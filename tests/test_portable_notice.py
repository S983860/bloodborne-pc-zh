"""Startup order and cancellation without opening UI or starting the game."""
import ctypes
import importlib.util
import os
import pathlib
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch

SOURCE=pathlib.Path(os.environ.get('BB_PORTABLE_SOURCE',
    str(pathlib.Path(__file__).resolve().parents[1]/'portable/engine')))
sys.path.insert(0,str(SOURCE))
spec=importlib.util.spec_from_file_location('notice_portable',SOURCE/'portable_game.py')
portable=importlib.util.module_from_spec(spec)
spec.loader.exec_module(portable)

class NoticeTests(unittest.TestCase):
    def test_direct_script_receives_native_session_owner(self):
        with tempfile.TemporaryDirectory() as tmp:
            engine=pathlib.Path(tmp)
            (engine/'bb-startup-notice.dll').touch()
            native=types.SimpleNamespace(BBConfirmStartupNotice=Mock(return_value=1),
                BBStartupNoticeOwnerPid=Mock(return_value=321))
            with patch.object(portable,'ENGINE',engine), \
                 patch.object(ctypes,'WinDLL',return_value=native), patch.dict(os.environ,{},clear=True):
                self.assertTrue(portable.confirm_notice())
                self.assertEqual(os.environ['BB_NOTICE_OWNER_PID'],'321')

    def test_cancel_stops_before_logs_hardware_settings_and_controls(self):
        with patch.object(sys,'argv',['portable_game.py']), \
             patch.object(portable,'confirm_notice',return_value=False) as notice, \
             patch.object(portable,'LOGS') as logs, \
             patch.object(portable,'environment') as environment:
            self.assertEqual(portable.main(),0)
            self.assertEqual(portable.main(),0)
            self.assertEqual(notice.call_count,2)
            logs.mkdir.assert_not_called()
            environment.assert_not_called()

    def test_missing_notice_component_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(portable,'ENGINE',pathlib.Path(tmp)):
            with self.assertRaisesRegex(RuntimeError,'提示组件缺失'):
                portable.confirm_notice()

    def test_notice_errors_fail_closed(self):
        for code in (-1,2,20):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as tmp:
                engine=pathlib.Path(tmp)
                (engine/'bb-startup-notice.dll').touch()
                native=types.SimpleNamespace(BBConfirmStartupNotice=Mock(return_value=code))
                with patch.object(portable,'ENGINE',engine), \
                     patch.object(ctypes,'WinDLL',return_value=native):
                    with self.assertRaisesRegex(RuntimeError,'已停止启动'):
                        portable.confirm_notice()

    def test_confirmation_then_hardware_then_controls(self):
        events=[]
        kernel=Mock()
        kernel.CreateMutexW.return_value=123
        hardware_module=types.SimpleNamespace(configure=lambda *a:events.append('hardware') or {})
        flow_module=types.SimpleNamespace(start_game=lambda *a:events.append('controls') or 0)
        with tempfile.TemporaryDirectory() as tmp, patch.object(sys,'argv',['portable_game.py']), \
             patch.object(portable,'LOGS',pathlib.Path(tmp)/'logs'), \
             patch.object(portable,'confirm_notice',side_effect=lambda:events.append('notice') or True), \
             patch.object(portable,'environment',side_effect=lambda:events.append('environment') or {}), \
             patch.object(ctypes,'WinDLL',return_value=kernel), \
             patch.object(ctypes,'get_last_error',return_value=0), \
             patch.dict(sys.modules,{'hardware_policy':hardware_module,'startup_flow':flow_module}):
            self.assertEqual(portable.main(),0)
        self.assertEqual(events,['notice','environment','hardware','controls'])

    def test_live_owner_is_forwarded_after_environment_sanitization(self):
        with tempfile.TemporaryDirectory() as tmp:
            engine=pathlib.Path(tmp)
            (engine/'defaults').mkdir()
            (engine/'defaults/environment.json').write_text('{}')
            with patch.object(portable,'ENGINE',engine), \
                 patch.object(portable,'display_size',return_value=(1920,1080)), \
                 patch.dict(os.environ,{'BB_NOTICE_OWNER_PID':'1234','BB_FAKE_SETTING':'1'}):
                env=portable.environment()
        self.assertEqual(env['BB_NOTICE_OWNER_PID'],'1234')
        self.assertNotIn('BB_FAKE_SETTING',env)

if __name__=='__main__':
    unittest.main(verbosity=2)
