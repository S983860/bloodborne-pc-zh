"""Mandatory native device selection before preparing or starting the game process."""
import datetime,json,pathlib,subprocess
from hardware_policy import read_ini,update_ini,validate_profile
from controller_profile import configure as configure_controller

def save_record(root,record):
    target=pathlib.Path(root)/'logs/session.json'
    temporary=target.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf8')
    temporary.replace(target)

def select_controls(root,env,log,hardware,run=None):
    root=pathlib.Path(root);engine=root/'engine';config=root/'settings/bbport.ini'
    validate_profile(hardware['adapter'])
    for name in ('bb-startup.exe','assets/fonts/NotoSerifSC.otf'):
        if not (engine/name).is_file():raise RuntimeError('启动前选择界面文件缺失：'+name+'。请复制完整的游戏文件夹。')
    settings=read_ini(config);adapter=hardware['adapter']
    configure_controller(root,env,log)
    args=[str(engine/'bb-startup.exe'),'--gpu',adapter['name'],'--vendor',str(adapter['vendor_id']),
          '--cpu',hardware.get('cpu','未知处理器'),'--ram',str(hardware.get('ram_mib',0)),
          '--vram',str(adapter.get('vram_mib',0)),'--upscaler',hardware['effective_upscaler'],
          '--neural','1' if hardware['neural_enhancement'] else '0',
          '--rt','1' if hardware['hardware_rt'] else '0',
          '--guard-mode','1' if settings.get('pad_guard_mode')=='1' else '0',
          '--initial','1' if settings.get('control_mode')=='1' else '0']
    print('Startup gate: hardware verified; waiting for control selection. Game process has not started.',file=log,flush=True)
    result=(run or subprocess.run)(args,cwd=engine,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=0x08000000)
    if result.returncode==20:
        print('Startup gate: cancelled; game process was not started.',file=log,flush=True)
        return None
    if result.returncode not in (10,11):raise RuntimeError('操作方式选择界面未能正常完成，游戏尚未启动。错误码：'+str(result.returncode))
    mode=result.returncode-10
    controller=configure_controller(root,env,log)
    updates={'control_mode':str(mode),'button_prompts':'2' if mode else '1'}
    if controller['exists']:updates['pad_icons']=controller['pad_icons']
    update_ini(config,updates)
    env.update(BB_CONTROL_CHOOSER='0',BB_CONTROLS_CONFIRMED='1')
    print('Startup gate: control selection saved: '+('controller' if mode else 'keyboard/mouse'),file=log,flush=True)
    return mode

def start_game(root,env,log,hardware,prepare,prepare_only=False,select=None,popen=None):
    root=pathlib.Path(root)
    record=dict(started=datetime.datetime.now().isoformat(),root=str(root),runtime=__import__('sys').executable,
                config=str(root/'settings/bbport.ini'),display=env['BB_DISPLAY_SIZE'],hardware=hardware,stage='hardware_checked')
    save_record(root,record)
    if not prepare_only:
        record['stage']='awaiting_controls';save_record(root,record)
        mode=(select or select_controls)(root,env,log,hardware)
        if mode is None:
            record.update(stage='cancelled',cancelled=True,exit_code=0,ended=datetime.datetime.now().isoformat())
            save_record(root,record)
            return 0
        record['control_mode']=mode
    record['stage']='preparing';save_record(root,record)
    command=prepare(env,log)
    record.update(engine=command[0],scene=env['BB_RENDER_RES'],output=env['BB_OUTPUT_RES'])
    if prepare_only:record.update(stage='prepared',prepared=True,exit_code=0)
    else:
        if env.get('BB_CONTROLS_CONFIRMED')!='1':raise RuntimeError('操作方式尚未确认，已阻止游戏启动。')
        print('Startup gate: checks and selection completed; starting game engine.',file=log,flush=True)
        child=(popen or subprocess.Popen)(command,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=0x08000000)
        record.update(stage='running',pid=child.pid);save_record(root,record)
        record.update(stage='ended',exit_code=child.wait(),ended=datetime.datetime.now().isoformat())
    save_record(root,record)
    return 0 if record['exit_code']==0 else 1
