"""Portable process-local controller mappings and explicit manual-mode choice."""
import pathlib,re
from hardware_policy import read_ini
def configure(root,env,log=None):
    settings=pathlib.Path(root)/'settings';profile=settings/'controller.ini';mappings=settings/'gamecontrollerdb.txt'
    env.update(BB_GAMEPAD_PROFILE=str(profile),BB_GAMEPAD_MAPPINGS=str(mappings),SDL_GAMECONTROLLERCONFIG_FILE=str(mappings))
    env.pop('BB_GAMEPAD',None);env.pop('BB_GAMEPAD_INDEX',None)
    try:values=read_ini(profile) if profile.stat().st_size<=8192 else {}
    except (OSError,ValueError,UnicodeError):values={}
    guid=values.get('preferred_guid','')
    if re.fullmatch('[0-9a-fA-F]{32}',guid):env['BB_GAMEPAD']=guid
    try:
        custom=mappings.read_text(encoding='utf-8-sig').splitlines() if mappings.stat().st_size<=1048576 else []
    except (OSError,ValueError,UnicodeError):custom=[]
    custom=[line for line in custom if re.match('[0-9a-fA-F]{32},',line) and 'platform:Windows,' in line]
    if custom:
        identities={line[:32].lower() for line in custom}
        inherited=[line for line in env.get('SDL_GAMECONTROLLERCONFIG','').splitlines() if line[:32].lower() not in identities]
        env['SDL_GAMECONTROLLERCONFIG']='\n'.join(inherited+custom)
    manual=values.get('manual_mode')=='1';env['BB_ALLOW_UNDETECTED_PAD']='1' if manual else '0'
    icons=values.get('pad_icons','auto')
    if icons not in ('auto','playstation','xbox'):icons='auto'
    if log is not None:print('Controller profile: manual='+str(manual)+' preferred='+env.get('BB_GAMEPAD','automatic'),file=log,flush=True)
    return dict(manual=manual,guid=env.get('BB_GAMEPAD',''),pad_icons=icons,exists=profile.is_file())
