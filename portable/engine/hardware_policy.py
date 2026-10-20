"""Portable startup policy based on the same Vulkan adapter selected by the engine."""
import ctypes,datetime,hashlib,json,os,pathlib,subprocess
VERSION=1
AMD_COMPATIBILITY_VERSION=3
AMD_FSR411_PROVIDER_SHA256='7bfd0e5382492f253d8b3da55fd5d552ac802370532364fcc08d5460d603c5af'
def validate_profile(profile):
    if not isinstance(profile,dict) or not isinstance(profile.get('vendor_id'),int) or not profile.get('name') or not isinstance(profile.get('index'),int):
        raise ValueError('显卡检测结果不完整，请检查显卡驱动后重试。')
    if profile.get('api_version',0)<((1<<22)|(3<<12)):
        raise ValueError('当前显卡驱动不支持游戏所需的 Vulkan 1.3，请先更新驱动或选择支持的显卡。')
def read_ini(path):
    return dict((k.strip(),v.strip()) for line in path.read_text(encoding='utf-8-sig').splitlines()
                if not line.lstrip().startswith('#') and '=' in line for k,v in [line.split('=',1)])
def update_ini(path,updates):
    lines=path.read_text(encoding='utf-8-sig').splitlines()
    lines=[line for line in lines if line.partition('=')[0].strip() not in updates]
    temp=path.with_suffix('.ini.tmp')
    temp.write_text('\n'.join(lines+[f'{k}={v}' for k,v in updates.items()])+'\n',encoding='utf8')
    os.replace(temp,path)
def choose(profile,settings,previous,provider):
    vendor=profile.get('vendor_id',0)
    rtx=vendor==0x10de and 'RTX' in profile.get('name','').upper()
    fsr411=bool(provider and profile.get('fsr411_features'))
    identity=f"{vendor:04x}:{profile.get('device_id',0):04x}"
    migrated=previous.get('version')!=VERSION or previous.get('identity')!=identity
    updates={}
    if migrated:
        updates.update(dlss_nr_enabled='0',hardware_rt_enabled='0',minimap_enabled='0',beginner_hints='1')
        if rtx:upscaler='dlss'
        else:upscaler='fsr3'
        # Keep output, colour, texture and effects choices; stop distributing the
        # 1/3-resolution RTX 5070 Ti-only experimental profile to other PCs.
        updates.update(upscaler=upscaler,preset='3' if profile.get('vram_mib',0)<=6144 else '2',dlss_sr_model='0',rt_samples='2',object_motion='1' if vendor==0x10de else '0')
    amd_revision=vendor==0x1002 and (migrated or previous.get('amd_compatibility_version')!=AMD_COMPATIBILITY_VERSION
        or (fsr411 and not previous.get('amd_trial_ready')))
    compatibility_reason=''
    if amd_revision:
        # This update is an explicitly requested FSR411 real-hardware trial.
        # `provider` on AMD means the separately packaged AMD-safe provider,
        # never the unmodified NVIDIA/default DLL. Configure backs up first.
        updates.update(upscaler='fsr411' if fsr411 else 'fsr3',
            preset='2' if profile.get('vram_mib',0)<=6144 else '1')
        if fsr411:
            updates.update(jitter='1',fsr4_auto_exposure='1',fsr4_invert_jitter='0')
        compatibility_reason='AMD_FSR411_SCALAR_POSTPASS_V1_TRIAL' if fsr411 else 'AMD_FSR411_TRIAL_PROVIDER_OR_FEATURES_MISSING'
    if not rtx:
        updates.update(dlss_nr_enabled='0',hardware_rt_enabled='0')
    if vendor!=0x10de:
        updates['object_motion']='0'
    # Validate the newly selected value, not the old PC's imported setting.
    selected=updates.get('upscaler',settings.get('upscaler'))
    if selected=='dlss' and not rtx:
        updates['upscaler']='fsr3'
    elif selected=='fsr411' and not fsr411:
        updates['upscaler']='fsr3'
    effective=dict(settings,**updates)
    return updates,dict(version=VERSION,identity=identity,adapter=profile,migrated=migrated,
                        amd_compatibility_version=AMD_COMPATIBILITY_VERSION if vendor==0x1002 else None,
                        amd_trial_ready=fsr411 if vendor==0x1002 else None,
                        compatibility_reason=compatibility_reason,
                        experimental_upscaler=effective.get('upscaler') in ('fsr4','fsr411'),
                        effective_upscaler=effective.get('upscaler','fsr3'),
                        neural_enhancement=effective.get('dlss_nr_enabled')=='1',hardware_rt=effective.get('hardware_rt_enabled')=='1')
def configure(root,env,log):
    root=pathlib.Path(root);engine=root/'engine';config=root/'settings/bbport.ini'
    try:
        result=subprocess.run([str(engine/'bb-gpu-capabilities.exe'),'--json'],cwd=engine,env=env,
          capture_output=True,text=True,encoding='utf8',errors='replace',timeout=20,creationflags=0x08000000,check=True)
        profile=json.loads(result.stdout)
        validate_profile(profile)
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        print('GPU detection failed; game launch blocked:',error,file=log,flush=True)
        raise RuntimeError('未能完成显卡兼容检查，游戏尚未启动。\n请安装或更新显卡的 Vulkan 驱动后重试。\n\n'+str(error)) from error
    cache=config.parent/'hardware_profile.json'
    try:previous=json.loads(cache.read_text(encoding='utf8'))
    except (OSError,ValueError):previous={}
    amd=profile.get('vendor_id')==0x1002
    provider_dir=engine/'fsr4vk/amd_compat' if amd else engine/'fsr4vk'
    provider=provider_dir/'amd_fidelityfx_upscaler_vk.dll'
    provider_hash=hashlib.sha256(provider.read_bytes()).hexdigest() if provider.is_file() else None
    provider_valid=bool(provider_hash and (not amd or provider_hash==AMD_FSR411_PROVIDER_SHA256))
    original=read_ini(config)
    updates,record=choose(profile,original,previous,provider_valid)
    if record['compatibility_reason'] and any(original.get(k)!=v for k,v in updates.items()):
        backups=config.parent/'compatibility_backups';backups.mkdir(exist_ok=True)
        name='amd_fsr411_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.ini'
        with (backups/name).open('xb') as out:out.write(config.read_bytes())
        record['previous_config_backup']='settings/compatibility_backups/'+name
    if updates:update_ini(config,updates)
    effective=dict(original,**updates)
    record['render_settings']={k:effective.get(k) for k in ('output_res','preset','jitter','reactive',
        'object_motion','fsr4_auto_exposure','fsr4_invert_jitter','sharpen','sharpness','hdr')}
    record['fsr411_provider_sha256']=provider_hash
    record['fsr411_provider_validated']=provider_valid
    if amd and not provider_valid and not record['compatibility_reason']:
        record['compatibility_reason']='AMD_FSR411_TRIAL_PROVIDER_MISSING_OR_HASH_MISMATCH'
    record['fsr411_provider_variant']='AMD-safe-v1' if amd else 'original-default'
    record['fsr411_provider_file']=str(provider.relative_to(root)).replace('\\','/')
    if amd:
        # Use the verified bundled payload and known RG16F motion contract.
        # Inherited capture/OptiScaler overrides must not silently swap them.
        legacy=('FSR4_VK_ASSET_ROOT','FSR4_VK_MOTION_FORMAT')
        record['ignored_legacy_fsr_overrides']=[name for name in legacy if name in env]
        for name in legacy:env.pop(name,None)
    record['logical_cpu_count']=os.cpu_count()
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
            record['cpu']=winreg.QueryValueEx(key,'ProcessorNameString')[0].strip()
        class Memory(ctypes.Structure):
            _fields_=[('length',ctypes.c_uint32),('load',ctypes.c_uint32),('physical',ctypes.c_uint64),('available',ctypes.c_uint64),('page',ctypes.c_uint64),('available_page',ctypes.c_uint64),('virtual',ctypes.c_uint64),('available_virtual',ctypes.c_uint64),('extended',ctypes.c_uint64)]
        mem=Memory();mem.length=ctypes.sizeof(mem)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(mem)):record['ram_mib']=mem.physical>>20
    except (ImportError,OSError):pass
    cache.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf8')
    rtx=profile.get('vendor_id')==0x10de and 'RTX' in profile.get('name','').upper()
    env.update(BB_HW_RT='1' if rtx else '0',BB_NATIVE_NR='1' if rtx else '0',
               BB_FSR4VK_DIR=str(provider_dir),BB_CONTROL_CHOOSER='1',BB_GPU_NAME=profile.get('name','未识别'))
    if 'index' in profile:env['BB_GPU_ID']=str(profile['index'])
    print('GPU startup profile:',json.dumps(record,ensure_ascii=False),file=log,flush=True)
    return record
