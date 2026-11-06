import unittest
from monitor_gateway import config_text, source_overrides, redact
from monitor_worker import command


class GatewayTest(unittest.TestCase):
    def test_all_channels_and_subtypes_and_password_encoding(self):
        values = source_overrides('p@ss&word', '192.0.2.1', 443, 'admin')
        self.assertEqual(len(values), 28)
        self.assertTrue(values['MTX_PATHS_RAW17_SOURCE'].endswith('channel=17&subtype=0'))
        self.assertTrue(values['MTX_PATHS_RAW30SUB_SOURCE'].endswith('channel=30&subtype=1'))
        self.assertIn('p%40ss%26word', values['MTX_PATHS_RAW17_SOURCE'])

    def test_config_is_on_demand_and_has_no_credentials(self):
        text = config_text('C:/Program Files/python.exe', 'D:/worker.py')
        self.assertEqual(text.count('runOnDemand:'), 56)
        self.assertEqual(text.count('sourceOnDemand: yes'), 28)
        self.assertNotIn('admin', text)
        overrides = source_overrides('test', '192.0.2.1', 443, 'admin')
        for key in overrides:
            path = key.removeprefix('MTX_PATHS_').removesuffix('_SOURCE').lower()
            self.assertIn(f'  {path}:\n', text)

    def test_worker_reads_only_local_proxy_and_supports_copy(self):
        args = command('cam30_sub', 'ffmpeg', 'h264')
        self.assertIn('rtsp://127.0.0.1:19554/raw30sub', args)
        self.assertIn('libx264', args)
        self.assertIn('copy', command('cam17', 'ffmpeg', 'copy'))
        vp9 = command('cam17_sub_vp9', 'ffmpeg', 'h264')
        self.assertIn('libvpx-vp9', vp9)
        self.assertIn('rtsp://127.0.0.1:19554/raw17sub', vp9)
        for invalid in ('cam16', 'cam31', 'cam17; bad', 'rtsp://example'):
            with self.assertRaises(ValueError):
                command(invalid, 'ffmpeg', 'h264')

    def test_redaction(self):
        value = redact('rtsp://admin:p%40ss@camera stream p@ss failed', 'p@ss')
        self.assertNotIn('p@ss', value)
        self.assertNotIn('p%40ss', value)
        self.assertNotIn('admin', value)


if __name__ == '__main__':
    unittest.main()
