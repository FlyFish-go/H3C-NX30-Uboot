/* SPDX-License-Identifier: GPL-2.0-or-later */
#define BDI_TEST 1
#define BDI_INSPECT_TEST 1
#ifndef BDI_INSPECT_PRODUCTION_TIMING
#define BDI_INSPECT_TIMEOUT_MS 500
#endif
#include "../src/bridge_dispatch.c"

#include <assert.h>
#include <time.h>

static const char *const SOURCE = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *const OTHER_REVISION = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const bdi_provider_record RECORD = {
    .candidate_id = "candidate-test",
    .hardware_class = "C",
    .source_sha256 = SOURCE,
    .source_revision_sha256 = SOURCE,
    .product = "TEST",
    .route = "route.test",
    .provider_path = "/fixture/not-ready",
};

static int child_mode(const char *mode)
{
    if (!strcmp(mode, "/fixture/early-eof")) {
        (void)write(STDOUT_FILENO, "not-ready\n", 10);
        close(STDOUT_FILENO);
        struct timespec pause_for = {0, 250000000L};
        while (nanosleep(&pause_for, &pause_for) && errno == EINTR) {}
        return 1;
    }
    if (!strcmp(mode, "/fixture/hang")) {
        for (;;) pause();
    }
    if (!strcmp(mode, "/fixture/signaled")) {
        (void)raise(SIGTERM);
        return 99;
    }
    if (!strcmp(mode, "/fixture/oversized")) {
        char text[1024];
        memset(text, 'X', sizeof(text));
        for (unsigned i = 0; i < 16; i++)
            (void)write(STDOUT_FILENO, text, sizeof(text));
        return 0;
    }
    if (!strcmp(mode, "/fixture/stage-malformed")) {
        static const char stages[] =
            "BI_INSPECT_STAGE nand start\n"
            "BI_INSPECT_STAGE malformed$token pass\n"
            "BI_INSPECT_STAGE nand pass\n";
        (void)write(STDERR_FILENO, stages, sizeof(stages) - 1);
        (void)write(STDOUT_FILENO, "{}\n", 3);
        return 0;
    }
    if (!strcmp(mode, "/fixture/late-stage")) {
        char noise[1024];
        memset(noise, 'N', sizeof(noise));
        for (unsigned i = 0; i < 70; i++)
            (void)write(STDERR_FILENO, noise, sizeof(noise));
        static const char stage[] = "\nBI_INSPECT_STAGE late_stage pass\n";
        (void)write(STDERR_FILENO, stage, sizeof(stage) - 1);
        (void)write(STDOUT_FILENO, "{}\n", 3);
        return 0;
    }
    if (!strcmp(mode, "/fixture/slow-valid")) {
        struct timespec pause_for = {16, 0};
        while (nanosleep(&pause_for, &pause_for) && errno == EINTR) {}
        static const char report[] =
            "{\"install_ready\":true,\"source_sha256\":"
            "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
            "\"product\":\"TEST\",\"route\":\"route.test\","
            "\"hardware_class\":\"C\",\"candidate_id\":\"candidate-test\"}\n";
        (void)write(STDOUT_FILENO, report, sizeof(report) - 1);
        return 0;
    }
    if (!strcmp(mode, "/fixture/wrong-report")) {
        (void)write(STDOUT_FILENO, "{}\n", 3);
        return 0;
    }
    if (!strcmp(mode, "/fixture/contradictory")) {
        static const char report[] =
            "{\"install_ready\":true,\"source_sha256\":"
            "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
            "\"product\":\"TEST\",\"route\":\"route.test\","
            "\"hardware_class\":\"C\",\"candidate_id\":\"candidate-test\","
            "\"install_ready\":false}\n";
        (void)write(STDOUT_FILENO, report, sizeof(report) - 1);
        return 0;
    }
    if (!strcmp(mode, "/fixture/trailing-garbage")) {
        static const char report[] =
            "{\"install_ready\":true,\"source_sha256\":"
            "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
            "\"product\":\"TEST\",\"route\":\"route.test\","
            "\"hardware_class\":\"C\",\"candidate_id\":\"candidate-test\"}garbage\n";
        (void)write(STDOUT_FILENO, report, sizeof(report) - 1);
        return 0;
    }
    if (!strcmp(mode, "/fixture/spoofed-rejection")) {
        static const char report[] =
            "{\"install_ready\":false,\"source_sha256\":\"wrong\","
            "\"product\":\"TEST\",\"route\":\"route.test\","
            "\"hardware_class\":\"C\",\"candidate_id\":\"candidate-test\","
            "\"rejected_check\":\"nand\",\"reason\":\"spoofed\"}\n";
        (void)write(STDOUT_FILENO, report, sizeof(report) - 1);
        return 1;
    }
    if (!strcmp(mode, "/fixture/wrong-exit")) {
        static const char report[] =
            "{\"install_ready\":true,\"source_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"product\":\"TEST\",\"route\":\"route.test\",\"hardware_class\":\"C\",\"candidate_id\":\"candidate-test\"}\n";
        (void)write(STDOUT_FILENO, report, sizeof(report) - 1);
        return 7;
    }
    if (!strcmp(mode, "/fixture/rejected-detail")) {
        static const char report[] =
            "{\"install_ready\":false,\"source_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
            "\"product\":\"TEST\",\"route\":\"route.test\",\"hardware_class\":\"C\","
            "\"candidate_id\":\"candidate-test\",\"rejected_check\":\"first_install_record\",\"reason\":\"BC/target\"}\n";
        (void)write(STDOUT_FILENO, report, sizeof(report) - 1);
        return 1;
    }
    if (!strcmp(mode, "/fixture/valid")) {
        static const char report[] =
            "{\"install_ready\":true,\"source_sha256\":"
            "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
            "\"product\":\"TEST\",\"route\":\"route.test\","
            "\"hardware_class\":\"C\",\"candidate_id\":\"candidate-test\"}\n";
        (void)write(STDOUT_FILENO, report, sizeof(report) - 1);
        return 0;
    }
    return 126;
}

static double monotonic_ms(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

static int run_fixture_revision(const char *mode, const char *revision, double *elapsed_ms)
{
    bdi_provider_record row = RECORD;
    row.provider_path = mode;
    row.source_revision_sha256 = revision;
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    double start = monotonic_ms();
    int result = inspect_provider(fd, &row);
    *elapsed_ms = monotonic_ms() - start;
    close(fd);
    return result;
}

static int run_fixture(const char *mode, double *elapsed_ms)
{
    return run_fixture_revision(mode, SOURCE, elapsed_ms);
}

static int run_nonexecutable_fixture(void)
{
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    bdi_provider_record row = RECORD;
    row.provider_path = "/fixture/nonexecutable";
    int result = inspect_provider(fd, &row);
    close(fd);
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--bridge-dispatch-inspect"))
        return child_mode(argv[0]);
    if (argc == 2 && !strcmp(argv[1], "--long-valid")) {
        double elapsed;
        int result = run_fixture("/fixture/slow-valid", &elapsed);
        assert(result == 1);
        assert(elapsed >= 16000.0 && elapsed < 30000.0);
        assert(g_inspect_diag.budget_ms == 180000);
        assert(g_inspect_diag.child_reaped);
        assert(!strcmp(g_inspect_diag.outcome, "ready"));
        printf("SLOW_VALID result=%d elapsed_ms=%.1f\n", result, elapsed);
        return 0;
    }
    double elapsed;
    int result = run_fixture("/fixture/early-eof", &elapsed);
    assert(result == -1);
    assert(elapsed >= 200.0 && elapsed < 1000.0);
    assert(!strcmp(g_inspect_diag.outcome, "report_invalid"));
    assert(g_inspect_diag.child_reaped);
    printf("EARLY_EOF result=%d elapsed_ms=%.1f\n", result, elapsed);

    result = run_fixture("/fixture/rejected-detail", &elapsed);
    assert(result == 0);
    assert(!strcmp(g_inspect_diag.outcome, "rejected"));
    assert(!strcmp(g_candidate_rejection,"C/first_install_record: BC/target"));
    assert(!strcmp(g_rejected_candidate_revision,SOURCE));
    char text[64];
    assert(!inspect_json_text("{\"reason\":\"quote \\\"x\\\"\"}","reason",text,sizeof(text)));
    assert(!strcmp(text,"quote \"x\""));
    assert(inspect_json_text("{\"reason\":\"bad\\n\"}","reason",text,sizeof(text)));
    assert(inspect_json_text("{\"reason\":\"too long\"}","reason",text,4));
    g_candidate_rejection[0]=0;g_rejected_candidate_revision[0]=0;g_rejected_candidates=0;
    remember_inspect_rejection("{\"source_sha256\":\"wrong\",\"reason\":\"spoofed\"}",&RECORD);
    assert(!g_candidate_rejection[0]);
    puts("REJECTION_DETAIL exact identity, escaped text and bounds PASS");

    snprintf(g_rejected_candidate_revision,sizeof(g_rejected_candidate_revision),"%s",SOURCE);
    result = run_fixture_revision("/fixture/hang", OTHER_REVISION, &elapsed);
    assert(result == -1);
    assert(elapsed >= 450.0 && elapsed < 2000.0);
    assert(!strcmp(g_inspect_diag.outcome, "timeout"));
    assert(g_inspect_diag.child_reaped);
    assert(g_inspect_diag.elapsed_ms >= 450 && g_inspect_diag.elapsed_ms < 2000);
    assert(!strcmp(g_inspect_revision, OTHER_REVISION));
    assert(!strcmp(g_rejected_candidate_revision, SOURCE));
    assert(strstr(g_inspect_detail, OTHER_REVISION));
    printf("HUNG_CHILD result=%d elapsed_ms=%.1f\n", result, elapsed);

    result = run_fixture("/fixture/wrong-report", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "report_invalid"));
    result = run_fixture("/fixture/contradictory", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "report_invalid"));
    result = run_fixture("/fixture/trailing-garbage", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "report_invalid"));
    g_candidate_rejection[0]=0;
    result = run_fixture("/fixture/spoofed-rejection", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "report_invalid"));
    assert(!g_candidate_rejection[0]);
    result = run_fixture("/fixture/wrong-exit", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "exit_error"));
    assert(g_inspect_diag.exit_code == 7);
    result = run_fixture("/fixture/signaled", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "signal"));
    assert(g_inspect_diag.term_signal == SIGTERM);
    result = run_fixture("/fixture/oversized", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "output_overflow"));
    assert(g_inspect_diag.child_reaped);
    result = run_fixture("/fixture/stage-malformed", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "report_invalid"));
    assert(!strcmp(g_inspect_diag.last_stage, "nand"));
    assert(g_inspect_diag.stderr_bytes > 0);
    result = run_fixture("/fixture/late-stage", &elapsed);
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "report_invalid"));
    assert(g_inspect_diag.stderr_truncated);
    assert(!strcmp(g_inspect_diag.last_stage, "late_stage"));
    assert(!strcmp(g_inspect_diag.last_status, "pass"));
    result = run_nonexecutable_fixture();
    assert(result == -1);
    assert(!strcmp(g_inspect_diag.outcome, "exec_error"));
    assert(g_inspect_diag.system_errno == EACCES || g_inspect_diag.system_errno == ENOEXEC);
    assert(g_inspect_diag.child_reaped);
    result = run_fixture("/fixture/valid", &elapsed);
    assert(result == 1);
    assert(!strcmp(g_inspect_diag.outcome, "ready"));
    assert(g_inspect_diag.child_reaped);
    printf("BAD_REPORT_AND_EXIT rejected; VALID_REPORT result=%d\n", result);
    return 0;
}
