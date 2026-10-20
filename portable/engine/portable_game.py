"""Private, offline game bootstrap; requires only the adjacent bundled runtime."""
import argparse,ctypes,datetime,hashlib,json,os,pathlib,subprocess,sys,traceback

ROOT=pathlib.Path(__file__).resolve().parent.parent
ENGINE=ROOT/'engine'
GAME=ROOT/'CUSA03173'
CONFIG=ROOT/'settings/bbport.ini'
ASSETS=ENGINE/'assets'
LOGS=ROOT/'logs'
CREATE_NO_WINDOW=0x08000000

def display_size():
    """Read the primary desktop mode in physical pixels, independent of Windows DPI scaling."""
    from ctypes import wintypes as w
    class Mode(ctypes.Structure):
        _fields_=[('device',w.WCHAR*32),('spec',w.WORD),('driver',w.WORD),
                  ('size',w.WORD),('extra',w.WORD),('fields',w.DWORD),
                  ('position',w.POINT),('orientation',w.DWORD),('fixed_output',w.DWORD),
                  ('color',w.SHORT),('duplex',w.SHORT),('y_resolution',w.SHORT),
                  ('tt_option',w.SHORT),('collate',w.SHORT),('form',w.WCHAR*32),
                  ('log_pixels',w.WORD),('bits',w.DWORD),('width',w.DWORD),('height',w.DWORD),
                  ('flags',w.DWORD),('frequency',w.DWORD),('icm_method',w.DWORD),
                  ('icm_intent',w.DWORD),('media',w.DWORD),('dither',w.DWORD),
                  ('reserved1',w.DWORD),('reserved2',w.DWORD),('pan_width',w.DWORD),('pan_height',w.DWORD)]
    user=ctypes.WinDLL('user32',use_last_error=True)
    query=user.EnumDisplaySettingsW
    query.argtypes=[w.LPCWSTR,w.DWORD,ctypes.POINTER(Mode)];query.restype=w.BOOL
    mode=Mode();mode.size=ctypes.sizeof(Mode)
    if query(None,0xffffffff,ctypes.byref(mode)) and 640<=mode.width<=16384 and 360<=mode.height<=8640:
        return mode.width&~1,mode.height&~1
    # Keep the fallback DPI aware too; restore the caller's thread context afterwards.
    context=user.SetThreadDpiAwarenessContext
    context.argtypes=[ctypes.c_void_p];context.restype=ctypes.c_void_p
    old=context(ctypes.c_void_p(-4))
    try:
        width,height=user.GetSystemMetrics(0),user.GetSystemMetrics(1)
        if 640<=width<=16384 and 360<=height<=8640:return width&~1,height&~1
        raise RuntimeError('无法读取屏幕的实际分辨率')
    finally:
        if old:context(old)

def environment():
    env={k:v for k,v in os.environ.items() if not k.startswith(('BB_','VK_','RESHADE_'))}
    env.update(json.loads((ENGINE/'defaults/environment.json').read_text(encoding='utf8')))
    env.update(BB_DATA_DIR=str(ROOT),BB_GAME_DIR=str(GAME),BB_USER_DIR=str(ROOT/'user'),BB_CONFIG=str(CONFIG),
               BB_MODS_ENABLED='0',BB_PC_MENU_ASSETS=str(ASSETS/'menus'),
               BB_DLSSNR_DIR=str(ENGINE),BB_DLSS_DIR=str(ENGINE),BB_HD_ASSETS=str(ASSETS/'hd'),BB_MINIMAP_ASSETS=str(ASSETS/'minimap'),BB_PC_CHINESE='1',
               BB_DMEM_MB='9152',PYTHONUNBUFFERED='1',BB_DISPLAY_SIZE='{}x{}'.format(*display_size()))
    windows=pathlib.Path(env.get('SystemRoot','C:/Windows'))
    env['PATH']=str(ENGINE)+';'+str(windows/'System32')+';'+str(windows)
    return env

def prepare(env,log):
    sys.path.insert(0,str(ENGINE/'scripts'))
    from engine_assets import verify_executable
    from patches import read_settings,scaled_sizes,render_size,output_size
    env['BB_ENGINE_IMAGE_SHA256']=verify_executable(ASSETS/'eboot.elf')
    settings=read_settings(CONFIG)
    if settings.get('output_res')=='auto':settings['output_res']=env['BB_DISPLAY_SIZE']
    sizes=scaled_sizes(settings)
    if sizes:
        (rw,rh),(ow,oh)=sizes
    else:
        ow,oh=output_size(settings)
        rw,rh=(ow,oh) if settings.get('upscaler')=='taa' else (render_size(settings) or (ow,oh))
    if (ow,oh)==(3200,1800) and settings.get('preset')=='3':rw,rh=1408,792
    rw,rh=(rw+3)&~3,(rh+3)&~3
    print(f"Display: physical={env['BB_DISPLAY_SIZE']}, output={ow}x{oh}, scene={rw}x{rh}",file=log,flush=True)
    patches_dir=ENGINE/'patches'
    subprocess.run([sys.executable,str(ENGINE/'scripts/patches.py'),'--out',str(ASSETS),'--fps','60',
                    '--settings',str(CONFIG),'--game-dir',str(GAME),'--render-res',f'{rw}x{rh}',
                    '--output-res',f'{ow}x{oh}','--patches-dir',str(patches_dir)],
                   env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=CREATE_NO_WINDOW,check=True)
    metadata=json.loads((ASSETS/'patches-info.json').read_text(encoding='utf8'))
    env.update(BB_RENDER_RES=f'{rw}x{rh}',BB_OUTPUT_RES=f'{ow}x{oh}',BB_UI_SIZE=metadata['ui_size'])
    env['BB_LOG_FILE']=str(LOGS/'last_session.log')
    return [str(ENGINE/'bb-probe.exe'),str(ASSETS/'boot-linked.bin'),'--content-profile',str(ASSETS/'content.bin'),
            '--patches',str(ASSETS/'patches.bin'),'--app0',str(GAME),'--user',str(ROOT/'user'),'--timeout','0']

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--prepare-only',action='store_true')
    parser.add_argument('--profile',choices=('3k','4k'))
    options=parser.parse_args()
    LOGS.mkdir(exist_ok=True)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateMutexW.argtypes=[ctypes.c_void_p,ctypes.c_bool,ctypes.c_wchar_p];kernel.CreateMutexW.restype=ctypes.c_void_p
    kernel.CloseHandle.argtypes=[ctypes.c_void_p];kernel.ReleaseMutex.argtypes=[ctypes.c_void_p]
    handle=kernel.CreateMutexW(None,True,'Local\\BloodbornePcPortable')
    if not handle:raise ctypes.WinError(ctypes.get_last_error())
    if ctypes.get_last_error()==183:
        kernel.CloseHandle(handle)
        ctypes.windll.user32.MessageBoxW(None,'游戏已经运行或正在启动。','血源诅咒',0x40)
        return 0
    try:
        if options.profile:
            overrides={'output_res':'3200x1800' if options.profile=='3k' else '3840x2160',
                       'preset':'3' if options.profile=='3k' else '4','upscaler':'dlss'}
            lines=CONFIG.read_text(encoding='utf8').splitlines()
            lines=[line for line in lines if line.partition('=')[0].strip() not in overrides]
            temporary=CONFIG.with_suffix('.ini.tmp')
            temporary.write_text('\n'.join(lines+[f'{key}={value}' for key,value in overrides.items()])+'\n',encoding='utf8')
            os.replace(temporary,CONFIG)
        previous=LOGS/'last_session.log'
        if previous.exists():
            history=LOGS/'history';history.mkdir(exist_ok=True)
            stamp=datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')
            previous.replace(history/f'{stamp}.log')
            if (LOGS/'session.json').exists():
                (LOGS/'session.json').replace(history/f'{stamp}.json')
        with (LOGS/'last_session.log').open('w',encoding='utf8') as log:
            env=environment()
            from hardware_policy import configure
            hardware=configure(ROOT,env,log)
            from startup_flow import start_game
            return start_game(ROOT,env,log,hardware,prepare,options.prepare_only)
    finally:
        kernel.ReleaseMutex(handle);kernel.CloseHandle(handle)

if __name__=='__main__':
    try:
        code=main()
        if code:
            ctypes.windll.user32.MessageBoxW(None,'游戏未能正常运行，请保留 logs 中的记录以便检查。','血源诅咒',0x10)
        raise SystemExit(code)
    except Exception as error:
        LOGS.mkdir(exist_ok=True)
        (LOGS/'startup_error.txt').write_text(traceback.format_exc(),encoding='utf8')
        ctypes.windll.user32.MessageBoxW(None,str(error)+'\n\n详细记录已保存到 logs\\startup_error.txt。','血源诅咒 · 启动检查',0x10)
        raise SystemExit(1)
