// A tiny VST2 plugin used only by FreeDAWm's tests. Built from FreeDAWm's own
// clean-room interface header, so the host is tested against the same ABI.
//
//   param 0 "Gain"  : output = input * (2 * value)
//   param 1 "Tempo" : read-only, tempo seen through host::getTime / 1000
//   param 2 "Notes" : read-only, note-ons received / 100
//
// Built with FDM_TEST_SHELL it becomes a shell holding two plugins (ids 1001, 1002)
// whose gain is multiplied by 1 or 3, picked through host::currentId.

#include <cstdio>
#include <cstring>

#include "../Source/vst2/Vst2Abi.h"

using namespace fdm::vst2;

#if defined(_WIN32)
 #define EXPORT extern "C" __declspec(dllexport)
#else
 #define EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

struct TestPlugin {
    Plugin vst {};
    HostFn host = nullptr;
    float params[3] = {0.5f, 0.0f, 0.0f};
    float multiplier = 1.0f;
    int notes = 0;
    int program = 0;
    float chunk[2] = {};
};

TestPlugin* self(Plugin* p) { return static_cast<TestPlugin*>(p->pluginObject); }

void copyText(void* ptr, const char* text) { std::strcpy(static_cast<char*>(ptr), text); }

iptr FDM_VST2_CALL dispatch(Plugin* p, i32 opcode, i32 index, iptr value, void* ptr, float) {
    TestPlugin* t = self(p);
    switch (opcode) {
        case op::close: delete t; return 1;
        case op::getEffectName: copyText(ptr, t->multiplier > 2.0f ? "FDM Shell B" : (t->multiplier > 1.0f ? "?" : "FDM Test Gain")); return 1;
        case op::getVendor: copyText(ptr, "FreeDAWm Tests"); return 1;
        case op::getProduct: copyText(ptr, "FDM Test"); return 1;
        case op::getVendorVersion: return 1000;
        case op::getCategory:
           #ifdef FDM_TEST_SHELL
            if (t->multiplier == 0.0f) return category::shell;
           #endif
            return category::effect;
        case op::getParamName: copyText(ptr, index == 0 ? "Gain" : (index == 1 ? "Tempo" : "Notes")); return 1;
        case op::getParamUnit: copyText(ptr, index == 0 ? "x" : ""); return 1;
        case op::getParamText: std::snprintf(static_cast<char*>(ptr), 24, "%.2f", static_cast<double>(t->params[index] * 2.0f)); return 1;
        case op::getProgram: return t->program;
        case op::setProgram: t->program = static_cast<int>(value); return 1;
        case op::getProgramNameAt: std::snprintf(static_cast<char*>(ptr), 24, "Preset %d", index + 1); return 1;
        case op::getChunk:
            t->chunk[0] = 4242.0f;
            t->chunk[1] = t->params[0];
            *static_cast<void**>(ptr) = t->chunk;
            return sizeof(t->chunk);
        case op::setChunk: {
            if (value != sizeof(t->chunk)) return 0;
            const float* c = static_cast<const float*>(ptr);
            if (c[0] == 4242.0f) t->params[0] = c[1];
            return 1;
        }
        case op::canDo: return std::strcmp(static_cast<const char*>(ptr), "receiveVstMidiEvent") == 0 ? 1 : 0;
        case op::sendEvents: {
            auto* list = static_cast<Events*>(ptr);
            auto** slots = reinterpret_cast<MidiEvent**>(reinterpret_cast<char*>(list) + offsetof(Events, events));
            for (int i = 0; i < list->count; ++i)
                if ((slots[i]->data[0] & 0xf0) == 0x90 && slots[i]->data[2] != 0) ++t->notes;
            t->params[2] = static_cast<float>(t->notes) / 100.0f;
            return 1;
        }
#ifdef FDM_TEST_SHELL
        case op::shellNextPlugin: {
            static int next = 0;
            if (next >= 2) { next = 0; return 0; }
            copyText(ptr, next == 0 ? "FDM Shell A" : "FDM Shell B");
            return 1001 + next++;
        }
#endif
        default: return 0;
    }
}

void FDM_VST2_CALL setParameter(Plugin* p, i32 index, float v) { if (index == 0) self(p)->params[0] = v; }
float FDM_VST2_CALL getParameter(Plugin* p, i32 index) { return self(p)->params[index]; }

void FDM_VST2_CALL processReplacing(Plugin* p, float** in, float** out, i32 frames) {
    TestPlugin* t = self(p);
    if (auto* time = reinterpret_cast<TimeInfo*>(t->host(p, host::getTime, 0, 0, nullptr, 0.0f)))
        t->params[1] = static_cast<float>(time->tempo / 1000.0);
    const float gain = 2.0f * t->params[0] * t->multiplier;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i) out[c][i] = in[c][i] * gain;
}

}  // namespace

EXPORT Plugin* VSTPluginMain(HostFn host) {
    if (host(nullptr, host::version, 0, 0, nullptr, 0.0f) < 2400) return nullptr;
    auto* t = new TestPlugin();
    t->host = host;
#ifdef FDM_TEST_SHELL
    const auto id = host(nullptr, host::currentId, 0, 0, nullptr, 0.0f);
    t->multiplier = id == 1001 ? 1.0f : (id == 1002 ? 3.0f : 0.0f);      // 0 = the shell itself
#endif
    Plugin& v = t->vst;
    v.magic = kMagic;
    v.dispatch = dispatch;
    v.setParameter = setParameter;
    v.getParameter = getParameter;
    v.numPrograms = 3;
    v.numParams = 3;
    v.numInputs = 2;
    v.numOutputs = 2;
    v.flags = flag::canReplace | flag::chunks;
    v.latency = 0;
    v.pluginObject = t;
    v.uniqueId = 0x46446d54;
    v.pluginVersion = 1000;
    v.processReplacing = processReplacing;
    return &v;
}
