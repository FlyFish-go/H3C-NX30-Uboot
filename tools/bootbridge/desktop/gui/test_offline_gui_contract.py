#!/usr/bin/env python3
"""Static host checks for the isolated offline Windows GUI source contract."""
from pathlib import Path
import re
import tempfile
import unittest
import importlib.util
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT.parent
GUI = ROOT / "gui/offline_main.c"
SOURCE = GUI.read_text(encoding="utf-8")
STRINGS = PROJECT / "design/desktop-offline-ui/strings.json"
STRINGS_SOURCE = STRINGS.read_text(encoding="utf-8")
RULES_HEADER = ROOT / "offline/bb_offline_rules.h"
MANIFEST = ROOT / "gui/app.manifest"
RESOURCE = ROOT / "gui/resources.rc"
BUILD = ROOT / "tools/build-windows-offline.sh"
GENERATED = ROOT / "tools/build-offline-generated.py"
UI_TEXT = ROOT / "gui/bb_ui_text.c"
UI_TEXT_HEADER = ROOT / "gui/bb_ui_text.h"
UI_TEXT_TEST = ROOT / "gui/host-tests/test_bb_ui_text.c"
PE_CHECK = ROOT / "tools/check-offline-pe.sh"
NET_HEADER = ROOT / "src/bb_net.h"
NET_POLICY = ROOT / "src/bb_net_policy.c"
NET_WIN = ROOT / "src/bb_net_win.c"
FETCH = ROOT / "src/bb_fetch.c"
FETCH_HEADER = ROOT / "src/bb_fetch.h"
OFFICIAL_API = ROOT / "src/official_api.c"
NET_POLICY_TEST = ROOT / "gui/host-tests/test_bb_net_policy.c"
NET_SOURCES = (NET_HEADER, NET_POLICY, NET_WIN, FETCH, FETCH_HEADER, OFFICIAL_API)


class OfflineGuiContract(unittest.TestCase):
    def test_generated_strings_have_no_markup_or_placeholders(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "bb_ui_strings_generated.h"
            subprocess.run([sys.executable, str(ROOT / "tools/gen_ui_strings.py"),
                            str(STRINGS), str(output)], check=True)
            generated = output.read_text(encoding="utf-8")
            self.assertNotIn("<", generated)
            self.assertNotIn(">", generated)
            self.assertNotRegex(generated, r"\{(?:m|v|n|f|ver)\}")
            self.assertIn("bb_strings_zh_dropS_runs", generated)
            self.assertIn("bb_strings_en_s3warn_runs", generated)
            # Ordered styled text (R2-5): markup survives as BB_UI_MARK_* codes.
            self.assertIn("bb_strings_zh_marked", generated)
            self.assertIn("bb_strings_en_marked", generated)
            self.assertNotRegex(generated, r"\\x[0-9A-Fa-f]")

    def test_generated_string_ids_match_their_text(self):
        # Regression: list-valued keys once took enum slots without table
        # entries, shifting every later ID (settings showed wrong labels).
        import json
        data = json.loads(STRINGS_SOURCE)
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "bb_ui_strings_generated.h"
            subprocess.run([sys.executable, str(ROOT / "tools/gen_ui_strings.py"),
                            str(STRINGS), str(output)], check=True)
            generated = output.read_text(encoding="utf-8")
        enum = re.search(r"typedef enum \{(.*?)BB_STR_COUNT", generated, re.S).group(1)
        ids = [name.strip() for name in enum.split(",") if name.strip()]
        scalar = [key for key, value in data["zh"].items() if isinstance(value, str)]
        self.assertEqual(ids, ["BB_STR_" + key.upper() for key in scalar])
        for lang in ("zh", "en"):
            body = generated.split("bb_strings_%s[BB_STR_COUNT] = {" % lang, 1)[1].split("};", 1)[0]
            entries = dict(re.findall(r"^    \[(BB_STR_\w+)\] = L\"(.*)\",$", body, re.M))
            self.assertEqual(sorted(entries), sorted(ids))
            for key in ("mLang", "mTheme", "mUpdate", "mDiag", "mAbout", "copyDiag", "ambiguous"):
                self.assertEqual(entries["BB_STR_" + key.upper()], data[lang][key])

    def test_styled_paragraphs_keep_order_size_and_alignment(self):
        for key in ("DROPS", "S3WARN", "S6BACKUP"):
            self.assertIn("TM(BB_STR_%s)" % key, SOURCE)
            self.assertNotIn("T(BB_STR_%s)" % key, SOURCE.replace("TM(BB_STR_%s)" % key, ""))
        self.assertIn("strong_font_for(", SOURCE)
        self.assertIn("BB_UI_ALIGN_CENTER", SOURCE)
        self.assertIn("bb_ui_line_offset(", SOURCE)
        self.assertIn("fallback_paragraph(", SOURCE)

    def test_no_per_pixel_gdi_and_live_drop_hover(self):
        self.assertNotRegex(SOURCE, r"\bSetPixel(?:V)?\s*\(")
        self.assertIn("GdipFillRectangleI(", SOURCE)
        self.assertNotIn("drag_over", SOURCE)
        self.assertIn("hover_is(EL_PICK)", SOURCE)
        # state.hover is an element index; comparing it with an el_kind was wrong.
        self.assertNotRegex(SOURCE, r"state\.hover\s*==\s*EL_")
        self.assertIn("state.hover_kind = hit >= 0 ? elements[hit].kind : EL_NONE", SOURCE)

    def test_min_track_size_uses_window_monitor_and_dpi(self):
        block = SOURCE.split("case WM_GETMINMAXINFO:", 1)[1].split("case WM_", 1)[0]
        self.assertIn("MonitorFromWindow(hwnd", block)
        self.assertIn("window_dpi(hwnd)", block)
        self.assertNotIn("POINT origin = {0, 0}", block)

    def test_no_ui_string_is_used_as_a_format_string(self):
        for match in re.finditer(r"swprintf\s*\(", SOURCE):
            args = SOURCE[match.end():].split(";", 1)[0]
            self.assertNotRegex(args, r"^\s*[^,]+,\s*[^,]+,\s*T\s*\(\s*BB_STR_", args)
        self.assertIn("format_checked(", SOURCE)
        self.assertIn("bb_ui_format(", SOURCE)

    def test_host_layout_wrap_and_modal_hit_tests(self):
        executable = Path(tempfile.gettempdir()) / "bb_ui_text_host_test"
        subprocess.run(["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                        "-Wall", "-Wextra", "-Werror", "-Wformat=2",
                        str(UI_TEXT), str(UI_TEXT_TEST), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        self.assertIn("bb_ui_clamp_scroll", UI_TEXT_HEADER.read_text(encoding="utf-8"))
        self.assertIn("bb_ui_hit_element", UI_TEXT_HEADER.read_text(encoding="utf-8"))
        self.assertIn("bb_ui_modal_layout", UI_TEXT_HEADER.read_text(encoding="utf-8"))

    def test_all_steps_scroll_and_scrollbar_uses_content_minus_viewport(self):
        self.assertIn("int scrollable = 1;", SOURCE)
        self.assertIn("bb_ui_clamp_scroll(INT_MAX, state.content_height, viewport)", SOURCE)
        self.assertIn("int thumb_height", SOURCE)
        self.assertNotIn("state.step == 3 || state.step == 6", SOURCE)

    def test_modal_hit_is_layer_filtered_and_modal_scrolls(self):
        self.assertIn("add_modal_element", SOURCE)
        self.assertIn("bb_ui_hit_element(rects", SOURCE)
        self.assertIn("if (state.modal && hit < 0)", SOURCE)
        self.assertIn("state.modal_scroll", SOURCE)
        self.assertIn("bb_ui_modal_layout", SOURCE)

    def test_path_privacy_tracking_and_real_progress(self):
        self.assertIn("base_name(state.input)", SOURCE)
        self.assertNotRegex(SOURCE, r"draw_text\([^;]*,\s*state\.input\s*,[^;]*DT_SINGLELINE")
        self.assertIn("TrackMouseEvent(&track)", SOURCE)
        self.assertIn("active->size", SOURCE)
        self.assertIn("state.hash_total", SOURCE)
        self.assertIn("state.building_percent = (state.building_percent + 4) % 100", SOURCE)

    def test_focus_is_kind_and_index_not_array_position(self):
        self.assertIn("el_kind focus_kind", SOURCE)
        self.assertIn("int focus_index", SOURCE)
        self.assertIn("find_focus()", SOURCE)
        self.assertNotRegex(SOURCE, r"state\.focus\s*=\s*hit")
        self.assertIn("GetKeyState(VK_SHIFT)", SOURCE)

    def test_gdiplus_is_whitelisted_and_linked(self):
        pe = PE_CHECK.read_text(encoding="utf-8")
        self.assertIn('"gdiplus.dll"', pe)
        self.assertIn("-lgdiplus", BUILD.read_text(encoding="utf-8"))
        self.assertIn("-lgdiplus", GENERATED.read_text(encoding="utf-8"))

    def test_winhttp_is_whitelisted_and_network_sources_are_linked(self):
        pe = PE_CHECK.read_text(encoding="utf-8")
        self.assertIn('"winhttp.dll"', pe)
        self.assertNotRegex(pe, r"wininet|urlmon")
        for script in (BUILD.read_text(encoding="utf-8"), GENERATED.read_text(encoding="utf-8")):
            self.assertIn("-lwinhttp", script)
            for name in ("src/bb_fetch.c", "src/bb_net_policy.c", "src/bb_net_win.c", "src/official_api.c"):
                self.assertIn(name, script)

    def test_real_v2_source_manifest_resolves_every_project_path(self):
        spec = importlib.util.spec_from_file_location("offline_generated_manifest", GENERATED)
        generated = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generated)
        _, inputs = generated.verify_source_snapshot(ROOT, PROJECT)
        manifest = __import__("json").loads((ROOT / "SOURCE-MANIFEST.json").read_text())
        self.assertEqual(len(inputs), manifest["source_count"])
        self.assertIn("gui/offline_main.c", inputs)
        self.assertIn("design/desktop-offline-ui/strings.json", inputs)
        self.assertEqual(inputs, generated.verify_source_snapshot(
            ROOT, PROJECT, ROOT / "SOURCE-MANIFEST.json")[1])

    def test_project_and_desktop_roots_are_not_conflated_in_build_inputs(self):
        build = BUILD.read_text(encoding="utf-8")
        self.assertIn('project_root=$(CDPATH= cd -- "$source_root/.." && pwd)', build)
        self.assertIn('"$project_root/design/desktop-offline-ui/strings.json"', build)
        self.assertNotIn('"$root/design/desktop-offline-ui/strings.json"', build)

        spec = importlib.util.spec_from_file_location("offline_generated_contract", GENERATED)
        generated = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generated)
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp)
            source = project / "desktop"
            source.mkdir()
            strings = project / "design/desktop-offline-ui/strings.json"
            strings.parent.mkdir(parents=True)
            strings.write_text("{}\n", encoding="utf-8")
            (source / "gui").mkdir()
            (source / "gui/offline_main.c").write_text("int x;\n", encoding="utf-8")
            manifest = {
                "schema": "bootbridge-offline-source-snapshot-v2",
                "status": "SOURCE_ONLY",
                "source_count": 2,
                "files": [],
            }
            for path, base in ((strings, project), (source / "gui/offline_main.c", source)):
                manifest["files"].append({
                    "path": str(path.relative_to(base)),
                    "bytes": path.stat().st_size,
                    "sha256": __import__("hashlib").sha256(path.read_bytes()).hexdigest(),
                })
            import json
            manifest["files"][1]["path"] = "desktop/gui/offline_main.c"
            (source / "SOURCE-MANIFEST.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "source snapshot is incomplete"):
                generated.verify_source_snapshot(source, project)
            manifest["schema"] = "bootbridge-offline-source-snapshot-v1"
            manifest["files"][0]["path"] = "gui/offline_main.c"
            (source / "SOURCE-MANIFEST.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises((FileNotFoundError, ValueError)):
                generated.verify_source_snapshot(source, project)

    def test_offline_entrypoint_has_no_download_ota_or_hidden_shell_links(self):
        forbidden = re.compile(
            r"official_download|transport|\bOTA\b|author|WinHTTP|WinINet|"
            r"CreateProcess|WinExec|URLDownload|github\.com|ID_UPDATE|"
            r"ID_AUTHOR|ID_DOWNLOAD",
            re.IGNORECASE,
        )
        self.assertIsNone(forbidden.search(SOURCE))
        self.assertIn("int WINAPI wWinMain", SOURCE)
        self.assertNotIn('#include "official_download.h"', SOURCE)
        self.assertNotIn("../ota/", SOURCE)
        self.assertNotIn("../transport/", SOURCE)
        # The GUI reaches the network only through the download facade.
        includes = set(re.findall(r'#include\s+"([^"]+)"', SOURCE))
        self.assertIn("../src/bb_fetch.h", includes)
        self.assertFalse({"../src/bb_net_win.c", "../src/official_api.h", "official_api.h"} & includes)
        self.assertNotRegex(SOURCE, r"#include\s+<win(?:http|inet)\.h>")

    def test_network_module_is_pinned_to_reviewed_h3c_endpoints(self):
        text = "\n".join(path.read_text(encoding="utf-8") for path in NET_SOURCES)
        hosts = set(re.findall(r'"(?:https?://)?([A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+\.(?:com|cn|net|org))', text))
        self.assertEqual(hosts, {"magic.h3c.com", "h3c-magic.oss-cn-hangzhou.aliyuncs.com"})
        header = NET_HEADER.read_text(encoding="utf-8")
        self.assertIn('#define BBN_QUERY_HOST "magic.h3c.com"', header)
        self.assertIn('BBN_STATIC_PREFIX "https://" BBN_OBJECT_HOST "/gateway-version/"', header)
        forbidden = re.compile(
            r"ShellExecute|CreateProcess|WinExec|URLDownload|InternetOpen|LoadLibrary|"
            r"SECURITY_FLAG_IGNORE|WINHTTP_OPTION_SECURITY_FLAGS|github\.com|RegSetValue|WriteProfile",
            re.IGNORECASE,
        )
        self.assertIsNone(forbidden.search(text))
        # NX30 R012: the user types only the SN; BootBridge asks as R011, then R012.
        self.assertIn('nx30_r012_from[] = {"NX30V100R011", "NX30V100R012"};',
                      NET_POLICY.read_text(encoding="utf-8"))
        self.assertIn("result.status != BBF_SERVER_REJECTED && result.status != BBF_OTHER_VERSION",
                      FETCH.read_text(encoding="utf-8"))
        self.assertNotIn("current_version", FETCH_HEADER.read_text(encoding="utf-8"))
        win = NET_WIN.read_text(encoding="utf-8")
        self.assertIn("WINHTTP_FLAG_SECURE", win)
        self.assertIn("WINHTTP_DISABLE_REDIRECTS", win)
        self.assertNotIn('L"http://', win)

    def test_download_is_size_and_hash_verified_before_it_is_renamed(self):
        fetch = FETCH.read_text(encoding="utf-8")
        self.assertIn("CREATE_NEW", fetch)
        self.assertLess(fetch.index("sink.written != request->expected_bytes"), fetch.index("MoveFileExW("))
        self.assertLess(fetch.index("memcmp(digest, request->expected_sha256, 32)"), fetch.index("MoveFileExW("))
        self.assertIn("if (created) DeleteFileW(part);", fetch)
        self.assertIn("SecureZeroMemory", fetch)
        self.assertIn("hex_digest(m->sha256, j->expected)", SOURCE)
        self.assertIn("j->expected_bytes = m->bytes;", SOURCE)
        # A verified download is identified by the same offline rules as a dropped file.
        self.assertIn("if (state.rules_ok) begin_hash(hwnd, fetched);", SOURCE)

    def test_router_serial_is_never_persisted_or_logged(self):
        save = SOURCE[SOURCE.index("static void save_ini"):]
        save = save[:save.index("\n}\n") + 3]
        self.assertNotRegex(save, r"serial|sn_edit")
        self.assertNotRegex(SOURCE, r"last_error[^;]*serial")
        self.assertNotRegex(SOURCE, r"copy_clipboard\([^;]*serial")
        self.assertIn("SecureZeroMemory(j->serial,sizeof(j->serial));", SOURCE)
        self.assertIn("SecureZeroMemory(raw, sizeof(raw));", SOURCE)
        self.assertIn('if (sn_edit) SetWindowTextW(sn_edit, L"");', SOURCE)

    def test_network_policy_host_test(self):
        executable = Path(tempfile.gettempdir()) / "bb_net_policy_host_test"
        subprocess.run(["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                        "-Wall", "-Wextra", "-Werror", "-Wformat=2", "-I", str(ROOT / "src"),
                        str(NET_POLICY), str(OFFICIAL_API), str(NET_POLICY_TEST),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

    def test_shell_open_is_single_helper_and_only_allows_http_schemes(self):
        self.assertEqual(SOURCE.count("ShellExecuteW("), 1)
        self.assertRegex(
            SOURCE,
            r"static\s+void\s+bb_open_url\s*\([^)]*\)\s*\{"
            r"[^}]*valid_url\s*\([^)]*\)"
            r"[^}]*ShellExecuteW\s*\(",
            )
        concatenated = re.compile(
            r'L\s*"(?:https?://)?[^"]*"\s*(?:\s+L\s*"(?:https?://)?[^"]*"\s*)+'
        )
        self.assertIsNone(concatenated.search(SOURCE),
                          "splitting the maintenance URL across literals would evade the URL audit")
        urls = re.findall(r'L"((?:https?://)[^"]*)"', SOURCE)
        self.assertTrue(urls)
        self.assertEqual(set(urls), {"http://192.168.10.1/"})
        self.assertIn("supported_product_identities",
                      (ROOT / "tools/gen_guide_models.py").read_text(encoding="utf-8"))

    def test_rules_are_verified_once_before_any_select_or_export_action(self):
        self.assertTrue(RULES_HEADER.is_file(), "offline rules module header must be provided")
        self.assertEqual(SOURCE.count("bb_offline_rules_load_verify("), 1)
        create = SOURCE[SOURCE.index("case WM_CREATE:"):SOURCE.index("case WM_PAINT:")]
        self.assertIn("bb_offline_rules_load_verify(&offline_catalog", create)
        self.assertLess(create.index("bb_offline_rules_load_verify"), create.index("refresh(hwnd)"))
        self.assertIn("offline_catalog.schema && offline_catalog.bundles && offline_catalog.bundle_count", SOURCE)
        self.assertIn("bb_bundle_catalog offline_catalog", SOURCE)
        self.assertIn("ZeroMemory(&offline_catalog, sizeof(offline_catalog));", SOURCE)
        self.assertIn("state.rules_ok = 0;", SOURCE)

    def test_only_input_is_selected_and_bundle_ambiguity_is_refused(self):
        self.assertNotRegex(SOURCE, r"COMBOBOX|\bID_BOARD\b|ui_board")
        self.assertIn("bb_select_bundle(j->catalog,j->size,j->digest,&j->bundle)", SOURCE)
        self.assertNotRegex(SOURCE, r"bb_select_bundle\s*\([^;]*\bguide_model\b")
        self.assertNotRegex(SOURCE, r"bb_export_plan_bin\s*\([^;]*\bguide_model\b")
        self.assertIn("BB_SELECT_AMBIGUOUS_BUNDLE", SOURCE)
        self.assertIn(
            "此原件对应多个不同输出方案，离线目录未提供唯一合并包，已拒绝生成。",
            STRINGS_SOURCE,
        )
        self.assertIn("state.bundle = active->bundle", SOURCE)

    def test_hash_worker_and_selection_epoch_invalidate_stale_choices(self):
        self.assertIn("FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN", SOURCE)
        self.assertIn("FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT", SOURCE)
        self.assertIn("bb_windows_path_allowed(input)", SOURCE)
        self.assertIn("while(!j->error&&!cancelled(j))", SOURCE)
        self.assertIn("InterlockedExchange(&active->cancel, 1)", SOURCE)
        self.assertIn("++selection_epoch", SOURCE)
        self.assertIn("j->selection_epoch = selection_epoch;", SOURCE)
        self.assertIn("active->selection_epoch == selection_epoch", SOURCE)
        begin = SOURCE[SOURCE.index("static void begin_hash"):SOURCE.index("static void choose_file")]
        self.assertLess(begin.index("clear_selection(T(BB_STR_HASHING))"),
                        begin.index("j->selection_epoch = selection_epoch;"))

    def test_export_reuses_verified_core_and_never_overwrites(self):
        self.assertIn(
            "bb_export_plan_bin(input,out,j->bundle->plan,1,j->bundle->plan->id)",
            SOURCE,
        )
        self.assertIn("OPEN_EXISTING", SOURCE)
        self.assertIn("create ? CREATE_NEW : OPEN_EXISTING", (ROOT / "src/bb_export.c").read_text(encoding="utf-8"))
        self.assertIn("保存新桥接 BIN（不会覆盖已有文件）", STRINGS_SOURCE)
        self.assertIn("BB_STR_SAVETITLE", SOURCE)
        self.assertIn("BootBridge", STRINGS_SOURCE)
        self.assertIn("普通用户权限", STRINGS_SOURCE)
        self.assertNotIn("没有生成伴随文件", SOURCE + STRINGS_SOURCE)

    def test_close_waits_for_worker_and_only_hash_or_download_job_is_cancellable(self):
        close = SOURCE[SOURCE.index("case WM_CLOSE:"):SOURCE.index("case WM_DESTROY:")]
        self.assertIn("if (active)", close)
        self.assertIn("active->kind == JOB_SELECT", close)
        self.assertIn("active->kind == JOB_FETCH", close)
        self.assertNotIn("JOB_EXPORT", close)
        self.assertIn("closing = 1", close)
        self.assertIn("if (closing) DestroyWindow(hwnd)", SOURCE)
        self.assertNotIn("TerminateThread", SOURCE)
        self.assertIn("WaitForSingleObject(worker, INFINITE)", SOURCE)
        self.assertNotRegex(SOURCE, r"EL_CANCEL[^}]*JOB_EXPORT")

    def test_per_monitor_v2_and_versioninfo_are_declared(self):
        manifest = MANIFEST.read_text(encoding="utf-8")
        self.assertIn("PerMonitorV2, PerMonitor", manifest)
        self.assertIn("<dpiAware", manifest)
        self.assertIn("true/pm", manifest)
        self.assertIn("asInvoker", manifest)
        resource = RESOURCE.read_text(encoding="utf-8")
        for field in ("CompanyName", "ProductName", "FileDescription", "FileVersion", "ProductVersion"):
            self.assertIn(field, resource)

    def test_windows_version_and_username_redaction_are_in_diagnostics(self):
        self.assertIn("RtlGetVersion", SOURCE)
        self.assertNotIn("GetVersion()", SOURCE)
        self.assertIn('%USERNAME%', SOURCE)
        self.assertIn("Windows: %ls\\nDPI", SOURCE)
        self.assertNotIn("Windows: %lu.%lu", SOURCE)

    def test_single_job_completion_notification(self):
        self.assertEqual(SOURCE.count("PostMessageW(j->owner,WM_JOB_DONE,0,0);"), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
