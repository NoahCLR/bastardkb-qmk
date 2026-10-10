"""Execute the real modifier composer and both report send paths with fake HID."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(source, signature):
    """Extract one complete C function so the test executes production code."""
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class ReportOverrideTests(unittest.TestCase):
    def test_passthrough_override_and_restore_in_both_report_modes(self):
        """Check report overrides preserve modifier ownership and one-shot state."""
        source = (ROOT / 'quantum/action_util.c').read_text()
        functions = '\n'.join(function(source, signature) for signature in [
            'static uint8_t get_mods_for_report(void)', 'void send_6kro_report(void)',
            'void send_nkro_report(void)'])
        c = '''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#define NKRO_ENABLE
#define KEY_OVERRIDE_ENABLE
#define SPECULATIVE_HOLD
static uint8_t real_mods=1, weak_mods=2, oneshot_mods=4;
static uint8_t suppressed_mods=2, weak_override_mods=8;
static bool active=false;
static unsigned clears=0, reports=0;
typedef struct {uint8_t mods; uint8_t keys;} report_keyboard_t;
typedef report_keyboard_t report_nkro_t;
static report_keyboard_t keyboard={0}, nkro={0};
static report_keyboard_t *keyboard_report=&keyboard;
static report_nkro_t *nkro_report=&nkro;
static uint8_t get_speculative_mods(void) {return 16;}
static bool has_anykey(void) {return true;}
static void clear_oneshot_mods(void) {clears++; oneshot_mods=0;}
static bool keyboard_report_mods_override_user(uint8_t *mods) {*mods=32; return active;}
static void host_keyboard_send(report_keyboard_t *r) {reports++;}
static void host_nkro_send(report_nkro_t *r) {reports++;}
''' + functions + '''
int main(void) {
    send_6kro_report(); assert(keyboard.mods==29 && clears==1 && reports==1);
    oneshot_mods=4; active=true;
    send_6kro_report(); assert(keyboard.mods==32 && clears==1 && oneshot_mods==4);
    send_nkro_report(); assert(nkro.mods==32 && clears==1 && oneshot_mods==4);
    real_mods=64; weak_mods=128; // Ownership can change while overridden.
    active=false; send_nkro_report(); assert(nkro.mods==220 && clears==2);
    assert(real_mods==64 && weak_mods==128);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp)
            (path / 'test.c').write_text(c)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
