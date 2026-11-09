// A module as an earlier Tetrad saves it in a patch: built against a published version (tests/
// fixtures/make_fixtures.sh), it moves a few settings, lets the generator write the tracks, and
// prints the module's patch JSON. Uses only what 2.6.2 already has (HAS_DRUM: 2.7.0 and later).

#include "../src/Tetrad.hpp"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: save_fixture VERSION\n");
        return 2;
    }
    {
        Tetrad m;
        m.model = modelTetrad;
        m.id = 1;
        Module::SampleRateChangeEvent e{48000.f, 1.f / 48000.f};
        m.onSampleRateChange(e);
        for (int t = 0; t < kNumTracks; ++t) {
            m.params[trackParamIndex(t, kTrackEnabled)].setValue(1.f);
            m.params[trackParamIndex(t, kTrackSeed)].setValue(static_cast<float>(23 + 11 * t));
        }
        m.params[trackParamIndex(2, kTrackMode)].setValue(static_cast<float>(kModePoly));
#ifdef HAS_DRUM
        m.params[trackParamIndex(3, kTrackMode)].setValue(static_cast<float>(kModeDrum));
#endif
        for (int i = 0; i < 48000; ++i) {   // one second: the generator writes the tracks
            Module::ProcessArgs a{48000.f, 1.f / 48000.f, i};
            m.process(a);
        }
        // The module's entry as Rack writes it in a patch (Module::toJson, whose parameter values
        // go through the engine, absent here): id, plugin, model, version, params, data.
        json_t* j = json_object();
        json_object_set_new(j, "id", json_integer(m.id));
        json_object_set_new(j, "plugin", json_string("Fourfold"));
        json_object_set_new(j, "model", json_string("Tetrad"));
        json_object_set_new(j, "version", json_string(argv[1]));
        json_t* params = json_array();
        for (int i = 0; i < static_cast<int>(m.params.size()); ++i) {
            json_t* p = json_object();
            json_object_set_new(p, "value", json_real(m.params[i].getValue()));
            json_object_set_new(p, "id", json_integer(i));
            json_array_append_new(params, p);
        }
        json_object_set_new(j, "params", params);
        json_object_set_new(j, "data", m.dataToJson());
        char* s = json_dumps(j, JSON_INDENT(2) | JSON_SORT_KEYS);
        std::printf("%s\n", s);
        std::free(s);
        json_decref(j);
    }
    return 0;
}
