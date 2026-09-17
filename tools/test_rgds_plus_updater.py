"""Run in an isolated Linux container; no device or real card mounts required."""
import os
import configparser
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile


SOURCE = Path(__file__).resolve().parents[1] / "RGDSPlus/rgds_plus_official_launcher.sh"
LOGO = SOURCE.parent / "Imgs/ROCreader_RGDSPlus.png"


class UpdateInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="rgds-plus-update-")
        self.root = Path(self.temp.name)
        self.app = self.root / "ROCreader_RGDSPlus"
        self.app.mkdir()
        self.downloads = self.app / "Downloads"
        self.downloads.mkdir()
        self.launcher = self.root / "ROCreader_RGDSPlus.sh"
        shutil.copyfile(SOURCE, self.launcher)
        self.launcher.chmod(0o755)
        (self.root / "Imgs").mkdir()
        (self.root / "Imgs/ROCreader.png").write_bytes(b"other app logo")
        (self.app / "version.txt").write_text("ver2.64\n")
        (self.app / "rocreader_sdl").write_text("old binary")
        self.preserved = {
            "native_config.ini": "audio=1\n",
            "native_progress.tsv": "book\t42\n",
            "native_history.txt": "book\n",
            "native_keymap.ini": "user keymap\n",
            "online_sources.ini": "user source\n",
            "books/book.txt": "user book",
            "book_covers/book.png": "user cover",
        }
        for name, text in self.preserved.items():
            target = self.app / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)

    def tearDown(self):
        self.temp.cleanup()

    def package(self, version="ver2.65", embedded=None, binary=True, device="RGDS plus"):
        path = self.downloads / f"ROCreader{version} for {device}.zip"
        prefix = "Roms/APPS/ROCreader_RGDSPlus/"
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr(prefix + "version.txt", (embedded or version) + "\n")
            if binary:
                archive.writestr(prefix + "rocreader_sdl", "#!/bin/sh\nexit 0\n")
            archive.writestr(prefix + "native_config.ini", "must not overwrite\n")
            archive.writestr(prefix + "rgds_power_control.sh", "#!/bin/sh\nexit 0\n")
            archive.writestr("Roms/APPS/ROCreader_RGDSPlus.sh", SOURCE.read_bytes())
            archive.writestr("Roms/APPS/Imgs/ROCreader_RGDSPlus.png", LOGO.read_bytes())
        (self.downloads / "ROCreader_update_pending.txt").write_text(
            f"filename={path.name}\nversion={version}\n"
        )
        return path

    def run_installer(self, args=("--install-pending-update",), env=None):
        return subprocess.run(
            ["sh", str(self.launcher), *args], cwd=self.root,
            env=env, capture_output=True, text=True, timeout=30,
        )

    def assert_preserved(self):
        self.assertEqual((self.root / "Imgs/ROCreader.png").read_bytes(), b"other app logo")
        for name, text in self.preserved.items():
            self.assertEqual((self.app / name).read_text(), text, name)

    def test_install_and_preserve_data(self):
        package = self.package()
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.app / "version.txt").read_text(), "ver2.65\n")
        self.assertEqual((self.root / "Imgs/ROCreader_RGDSPlus.png").read_bytes(), LOGO.read_bytes())
        self.assertFalse(package.exists())
        self.assertFalse((self.downloads / "ROCreader_update_pending.txt").exists())
        self.assertIn("result=success", (self.app / "cache/update_boot_status.txt").read_text())
        self.assert_preserved()

    def test_boot_install_and_restart(self):
        self.package()
        binary = self.app / "rocreader_sdl"
        binary.write_text('#!/bin/sh\nsh -c "$ROCREADER_UPDATE_INSTALL_COMMAND" || exit 1\nexit 23\n')
        binary.chmod(0o755)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(self.root / "xdg"))
        result = self.run_installer(args=(), env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.app / "version.txt").read_text(), "ver2.65\n")
        self.assert_preserved()

    def test_same_version_not_reinstalled(self):
        self.package(version="ver2.64")
        result = self.run_installer()
        self.assertEqual(result.returncode, 0)
        self.assertEqual((self.app / "rocreader_sdl").read_text(), "old binary")

    def test_other_model_ignored(self):
        package = self.package(device="GKD350H Ultra")
        result = self.run_installer()
        self.assertEqual(result.returncode, 0)
        self.assertTrue(package.exists())
        self.assertTrue((self.downloads / "ROCreader_update_pending.txt").exists())
        self.assertEqual((self.app / "version.txt").read_text(), "ver2.64\n")

    def test_missing_binary_rejected(self):
        self.package(binary=False)
        self.assertNotEqual(self.run_installer().returncode, 0)
        self.assertEqual((self.app / "rocreader_sdl").read_text(), "old binary")
        self.assertEqual((self.app / "version.txt").read_text(), "ver2.64\n")

    def test_wrong_embedded_version_rejected(self):
        self.package(embedded="ver2.63")
        self.assertNotEqual(self.run_installer().returncode, 0)
        self.assertEqual((self.app / "rocreader_sdl").read_text(), "old binary")
        self.assertEqual((self.app / "version.txt").read_text(), "ver2.64\n")

    def test_copy_failure_not_reported_as_success(self):
        self.package()
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        cp = bin_dir / "cp"
        cp.write_text("#!/bin/sh\nexit 1\n")
        cp.chmod(0o755)
        env = dict(os.environ, PATH=str(bin_dir) + ":" + os.environ["PATH"])
        self.assertNotEqual(self.run_installer(env=env).returncode, 0)
        self.assertEqual((self.app / "rocreader_sdl").read_text(), "old binary")
        self.assertEqual((self.app / "version.txt").read_text(), "ver2.64\n")
        self.assertIn("result=failed", (self.app / "cache/update_boot_status.txt").read_text())
        self.assertTrue((self.downloads / "ROCreader_update_pending.txt").exists())
        self.assert_preserved()

    @unittest.skipUnless(os.environ.get("ROCREADER_TEST_DOWNLOADS"), "release package directory not provided")
    def test_actual_release_package(self):
        version = os.environ.get("ROCREADER_TEST_VERSION", "ver2.64")
        source = next(Path(os.environ["ROCREADER_TEST_DOWNLOADS"]).glob(f"*{version} for RGDS plus.zip"))
        package = self.downloads / source.name
        shutil.copyfile(source, package)
        (self.app / "version.txt").write_text("ver2.03\n")
        (self.downloads / "ROCreader_update_pending.txt").write_text(
            f"filename={package.name}\nversion={version}\n"
        )
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.app / "version.txt").read_text(), version + "\n")
        with zipfile.ZipFile(source) as archive:
            self.assertEqual(
                (self.app / "rocreader_sdl").read_bytes(),
                archive.read("Roms/APPS/ROCreader_RGDSPlus/rocreader_sdl"),
            )
            if tuple(map(int, version.removeprefix("ver").split("."))) >= (2, 68):
                defaults = archive.read("Roms/APPS/ROCreader_RGDSPlus/native_config.ini")
                self.assertNotIn(b"\r", defaults)
                config = configparser.ConfigParser()
                config.read_string("[defaults]\n" + defaults.decode("utf-8"))
                self.assertEqual(config["defaults"]["animations"], "1")
                self.assertEqual(config["defaults"]["lid_close_screen_off"], "1")
                self.assertEqual(
                    defaults,
                    (SOURCE.parent / "native_config.release.ini").read_bytes().replace(b"\r\n", b"\n"),
                )
        self.assertEqual(self.launcher.read_bytes(), SOURCE.read_bytes().replace(b"\r\n", b"\n"))
        self.assertEqual((self.root / "Imgs/ROCreader_RGDSPlus.png").read_bytes(), LOGO.read_bytes())
        self.assert_preserved()


if __name__ == "__main__":
    if not shutil.which("unzip") and not shutil.which("busybox"):
        raise SystemExit("Install unzip or busybox before running installer tests")
    unittest.main()
