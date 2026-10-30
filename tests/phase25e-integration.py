#!/usr/bin/env python3
"""Standalone tests: guest code is inspected, never executed or simulated."""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
from pathlib import Path
import os
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools/phase25e-inspect.py"
spec = importlib.util.spec_from_file_location("phase25e_inspect", MODULE)
if spec is None or spec.loader is None:
    raise RuntimeError("phase25e inspector missing")
inspect = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = inspect
spec.loader.exec_module(inspect)


class Phase25eIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = Path(os.environ.get("PHASE25E_RESULTS", str(ROOT / "build/phase25e"))).resolve()
        cls.expected = {"lib" + n + ".dylib" for n in
                        ("root", "derived", "worker", "category", "ledger", "pipeline")}
        cls.expected.update(("app_basic", "app_protocol", "app_lifecycle"))
        cls.images = {n: inspect.inspect_file(cls.out / n) for n in cls.expected}
        cls.evidence = json.loads((cls.out / "phase25e-evidence.json").read_text())

    def test_3_real_executables_and_6_dylibs(self):
        self.assertEqual(len(self.images), 9)
        for name, image in self.images.items():
            self.assertEqual(image.kind, "MH_EXECUTE" if name.startswith("app_") else "MH_DYLIB")
            self.assertGreater(image.size, 4096)
            self.assertGreater(image.rx_bytes, 0)
            self.assertEqual(len(image.sha256), 64)

    def test_objc2_cross_image_semantics_emitted(self):
        self.assertIn("__objc_protolist", self.images["libworker.dylib"].objc_sections)
        self.assertIn("__objc_superrefs", self.images["libderived.dylib"].objc_sections)
        self.assertIn("__objc_catlist", self.images["libcategory.dylib"].objc_sections)
        for name, data in (
            ("libroot.dylib", (b"load\0", b"initialize\0", b"setValue:\0")),
            ("libderived.dylib", (b"objc_msgSendSuper2\0",)),
            ("libworker.dylib", (b"measure:\0",)),
            ("libcategory.dylib", (b"categoryValue\0",)),
            ("libpipeline.dylib", (b"objc_release\0", b"calculate:\0")),
            ("app_basic", (b"objc_msgSend\0", b"objc_retain\0", b"objc_release\0")),
        ):
            blob = (self.out / name).read_bytes()
            for symbol in data:
                self.assertIn(symbol, blob, (name, symbol))

    def test_dependency_bootstrap_is_deterministic(self):
        for app in ("app_basic", "app_protocol", "app_lifecycle"):
            first = inspect.dependency_order(self.images, app)
            self.assertEqual(len(first), 7)
            self.assertEqual(first[-1], app)
            self.assertEqual(first, inspect.dependency_order(self.images, app))
            self.assertLess(first.index("libroot.dylib"), first.index("libderived.dylib"))

    def test_negative_injections_document_real_and_blocked_cases(self):
        faults = self.evidence["fail_closed_cases"]
        for key in ("wrong_cpu", "truncated_image", "invalid_load_command",
                    "missing_lc_main", "invalid_objc_section", "missing_dylib",
                    "bad_argv", "bad_env", "unsupported_objc_dispatch",
                    "unsupported_signature", "unresolved_selector",
                    "premature_teardown"):
            self.assertIn(key, faults)
        self.assertIn("objc metadata", faults["invalid_objc_section"])

    def test_startup_contract(self):
        inspect.validate_startup(("app", "basic"), ("MODE=phase25e",),
                                 ("exec=/App/phase25e",))
        for argv, env, apple in (
            ((), ("A=b",), ("a=b",)),
            (("bad\0name",), ("A=b",), ("a=b",)),
            (("app",), ("MISSING_EQUALS",), ("a=b",)),
            (("app",), ("A=" + "x" * 16385,), ("a=b",)),
        ):
            with self.assertRaises(inspect.InspectionFailure):
                inspect.validate_startup(argv, env, apple)

    def test_isolated_metadata_contexts_not_guest_processes(self):
        def catalog(seed):
            images = {name: inspect.inspect_file(self.out / name)
                      for name in sorted(self.expected)}
            assert inspect.dependency_order(images, "app_basic")[-1] == "app_basic"
            return (seed, images)
        with ThreadPoolExecutor(max_workers=8) as pool:
            futures = [pool.submit(catalog, i) for i in range(32)]
            results = [f.result(timeout=30) for f in futures]
        for _, images in results:
            self.assertEqual(images, self.images)
        self.assertEqual(self.evidence["benchmarks"]["live_guest_instances_after_validation"], 0)
        self.assertEqual(self.evidence["benchmarks"]["mapped_guest_memory_bytes"], 0)

    def test_no_fake_execution_claims(self):
        gate = self.evidence["native_execution"]
        self.assertEqual(gate["status"], "blocked_unverified_native_runtime")
        for key in ("darwin_objc_methods_executed", "darwin_objc_sends_executed",
                    "ownership_transitions_executed", "lifecycle_callbacks_executed",
                    "guest_instructions_traced", "host_callbacks_as_darwin_code"):
            self.assertEqual(gate[key], 0)
        self.assertEqual(self.evidence["expected_only_if_verified"]
                         ["actual_verified_application_starts"], 0)
        self.assertTrue(all(item["guest_instructions_executed"] == 0
                            for item in self.evidence["execution_trace"]))


if __name__ == "__main__":
    unittest.main()
