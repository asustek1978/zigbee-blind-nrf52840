"""OTA readiness must fail closed on existing HW-954 UF2 flash map."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "check_zigbee_ota_readiness.py"
SPEC = importlib.util.spec_from_file_location("ota_readiness", SCRIPT)
ota = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ota)


class TestOtaReadiness(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dts = (
            ROOT / "boards/others/nicenano_zigbee/nicenano_zigbee_nrf52840.dts"
        ).read_text(encoding="utf-8")
        cls.conf = (ROOT / "prj.conf").read_text(encoding="utf-8")

    def test_existing_hardware_must_not_ota(self):
        report = ota.inspect(self.dts, self.conf)
        self.assertFalse(report["zigbee_ota_ready"])
        self.assertFalse(report["ota_image_generation_allowed"])
        self.assertTrue(any("slot1_partition" in e for e in report["reasons"]))
        self.assertTrue(any("MCUboot" in e for e in report["reasons"]))
        self.assertTrue(any("physical HW-954" in e for e in report["reasons"]))

    def test_flash_mapping_is_preserved(self):
        report = ota.inspect(self.dts, self.conf)
        self.assertEqual(report["application_bytes_available"], 0x87000)
        self.assertEqual(report["protected_uf2_guard_bytes"], 0x40000)
        self.assertEqual(report["partitions"]["zboss_nvram"]["offset"], 0xED000)
        self.assertEqual(report["partitions"]["storage_partition"]["offset"], 0xF2000)
        self.assertEqual(report["partitions"]["uf2_bootloader_partition"]["offset"], 0xF4000)

    def test_unsafe_overlap_is_detected(self):
        modified = self.dts.replace(
            "reg = <0x00026000 0x00087000>;",
            "reg = <0x00026000 0x00088000>;",
            1,
        )
        report = ota.inspect(modified, self.conf)
        self.assertFalse(report["zigbee_ota_ready"])
        self.assertTrue(any("overlapping" in e for e in report["reasons"]))

    def test_no_fake_ota_enablement_by_flags_alone(self):
        conf = self.conf + "\nCONFIG_BOOTLOADER_MCUBOOT=y\nCONFIG_ZIGBEE_FOTA=y\n"
        report = ota.inspect(self.dts, conf)
        self.assertFalse(report["zigbee_ota_ready"])
        self.assertTrue(any("physical HW-954" in e for e in report["reasons"]))


if __name__ == "__main__":
    unittest.main()
