//
// Copyright 2019 Volker Böhm.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.

// a clone of mutable instruments' 'warps' module for maxmsp
// by volker böhm, okt 2019, https://vboehm.net

// Original code by Émilie Gillet, https://mutable-instruments.net/
#include <m_pd.h>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"
#ifdef __APPLE__
#include "Accelerate/Accelerate.h"
#endif

#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <optional>

//#define ENABLE_LFO_MODE
#pragma warning(disable : 4068)

using std::clamp;
using std::optional;

const size_t kBlockSize = plaits::kBlockSize;

double kSampleRate = 48000.0;
// static const double kCorrectedSampleRate = 47872.34;
double a0 = (440.0 / 8.0) / kSampleRate;

static t_class *this_class = nullptr;

struct t_myObj
{
    t_object m_obj; // pd object - always placed in first in the object's struct

    plaits::Voice *voice_;
    plaits::Modulations modulations;
    plaits::Patch patch;
    double transposition_;
    double octave_;
    long engine;
    short trigger_connected;
    short trigger_toggle;

    char *shared_buffer;
    size_t shared_buffer_bytes;
    t_outlet *info_out;

    double sr;
    int sigvs;

    double *out_tmp;
    double *aux_tmp;
    t_inlet *m_in2;
    t_inlet *m_in3;
    t_inlet *m_in4;
    t_inlet *m_in5;
    t_inlet *m_in6;
    t_inlet *m_trig;
    t_inlet *m_level;
    t_float m_f2;
    t_float m_f3;
    t_float m_f4;
    t_float m_f5;
    t_float m_f6;
    t_float f_trig;
    t_float f_level;
    t_outlet *m_out;
    t_outlet *m_aux;
    t_float m_f;
};

void myObj_choose_engine(t_myObj* self, t_floatarg e); 
void setSr(double newsr) {
            kSampleRate = newsr;
            a0 = (440.0 / 8.0) / kSampleRate;
}

static void *myObj_new(t_symbol *s, int argc, t_atom *argv)
{
    t_myObj *self = (t_myObj *)pd_new(this_class);

    if (self)
    {
        // 8 audio inputs
        self->m_in2 = signalinlet_new((t_object *)self, self->m_f2);
        self->m_in3 = signalinlet_new((t_object *)self, self->m_f3);
        self->m_in4 = signalinlet_new((t_object *)self, self->m_f4);
        self->m_in5 = signalinlet_new((t_object *)self, self->m_f5);
        self->m_in6 = signalinlet_new((t_object *)self, self->m_f6);
        self->m_trig = signalinlet_new((t_object *)self, self->f_trig);
        self->m_level = signalinlet_new((t_object *)self, self->f_level);

        self->m_out = outlet_new((t_object *)self, &s_signal); // 'out' output
        self->m_aux = outlet_new((t_object *)self, &s_signal); // 'aux' output
        self->info_out = outlet_new((t_object *)self, &s_anything);

        self->sigvs = sys_getblksize();
        
        
        if(self->sigvs < kBlockSize) {
            pd_error((t_object*)self,
                         "sigvs can't be smaller than %d samples\n", kBlockSize);
            delete self;
            self = NULL;
            return self;
        }

        self->sr = sys_getsr();
        if (self->sr <= 0)
            self->sr = 44100.0;
        
        
        setSr(self->sr);
        
        // init some params
        self->transposition_ = 0.;
        self->octave_ = 0.5;
        self->patch.note = 48.0;
        self->patch.harmonics = 0.1;

        // allocate memory
        self->shared_buffer_bytes = 32768;
        self->shared_buffer = (char *)getbytes(self->shared_buffer_bytes);

        self->out_tmp = (double *)getbytes(kBlockSize * sizeof(double));
        self->aux_tmp = (double *)getbytes(kBlockSize * sizeof(double));

        if (self->shared_buffer == NULL)
        {
            pd_error((t_object *)self, "mem alloc failed!");
            delete self;
            self = NULL;
            return self;
        }
        stmlib::BufferAllocator allocator(self->shared_buffer, self->shared_buffer_bytes);

        self->voice_ = new plaits::Voice;
        self->voice_->Init(&allocator);

        // attributes ====
        int argnum = 0;
        while (argc > 0)
        {
            if (argv->a_type == A_FLOAT)
            {
                t_float argval = atom_getfloatarg(0, argc, argv);
                switch (argnum)
                {
                case 0:
                    myObj_choose_engine(self, argval);
                    break;
                default:
                    break;
                };
                argnum++;
                argc--;
                argv++;
            }
            else if (argv->a_type == A_SYMBOL)
            {
                t_symbol *curarg = atom_getsymbolarg(0, argc, argv);
                if (strcmp(curarg->s_name, "@engine") == 0)
                {
                    if (argc >= 2)
                    {
                        t_float argval = atom_getfloatarg(1, argc, argv);
                        myObj_choose_engine(self, argval);
                        argc -= 2;
                        argv += 2;
                    }
                }
                else
                {
                    argc -= 2;
                    argv += 2;
                }
            }
            else
            {
                argc -= 1;
                argv += 1;
            }
        }
    }
    else
    {
        delete self;
        self = NULL;
    }
    // end of attributes
    
    return (void*)self;
}

void myObj_info(t_myObj *self, t_symbol s)
{
    plaits::Patch p = self->patch;
    plaits::Modulations m = self->modulations;

    logpost((t_object *)self, 3, "Patch ----------------------->");
    logpost((t_object *)self, 3, "note: %f", p.note);
    logpost((t_object *)self, 3, "harmonics: %f", p.harmonics);
    logpost((t_object *)self, 3, "timbre: %f", p.timbre);
    logpost((t_object *)self, 3, "morph: %f", p.morph);
    logpost((t_object *)self, 3, "freq_mod_amount: %f", p.frequency_modulation_amount);
    logpost((t_object *)self, 3, "timbre_mod_amount: %f", p.timbre_modulation_amount);
    logpost((t_object *)self, 3, "morph_mod_amount: %f", p.morph_modulation_amount);

    logpost((t_object *)self, 3, "engine: %d", p.engine);
    logpost((t_object *)self, 3, "decay: %f", p.decay);
    logpost((t_object *)self, 3, "lpg_colour: %f", p.lpg_colour);

    logpost((t_object *)self, 3, "Modulations ------------>");
    logpost((t_object *)self, 3, "engine: %f", m.engine);
    logpost((t_object *)self, 3, "note: %f", m.note);
    logpost((t_object *)self, 3, "frequency: %f", m.frequency);
    logpost((t_object *)self, 3, "harmonics: %f", m.harmonics);
    logpost((t_object *)self, 3, "timbre: %f", m.timbre);
    logpost((t_object *)self, 3, "morph: %f", m.morph);
    logpost((t_object *)self, 3, "trigger: %f", m.trigger);
    logpost((t_object *)self, 3, "level: %f", m.level);
    logpost((t_object *)self, 3, "freq_patched: %d", m.frequency_patched);
    logpost((t_object *)self, 3, "timbre_patched: %d", m.timbre_patched);
    logpost((t_object *)self, 3, "morph_patched: %d", m.morph_patched);
    logpost((t_object *)self, 3, "trigger_patched: %d", m.trigger_patched);
    logpost((t_object *)self, 3, "level_patched: %d", m.level_patched);
    logpost((t_object *)self, 3, "-----");
}

void calc_note(t_myObj *self)
{
#ifdef ENABLE_LFO_MODE
    int octave = static_cast<int>(self->octave_ * 10.0);
    if (octave == 0)
    {
        self->patch.note = -48.37 + self->transposition_ * 60.0;
    }
    else if (octave == 9)
    {
        self->patch.note = 60.0 + self->transposition_ * 48.0;
    }
    else
    {
        const double fine = self->transposition_ * 7.0;
        self->patch.note = fine + static_cast<double>(octave) * 12.0;
    }
#else
    int octave = static_cast<int>(self->octave_ * 9.0);
    if (octave < 8)
    {
        const double fine = self->transposition_ * 7.0;
        self->patch.note = fine + static_cast<float>(octave) * 12.0 + 12.0;
    }
    else
    {
        self->patch.note = 60.0 + self->transposition_ * 48.0;
    }
#endif // ENABLE_LFO_MODE
}

// plug / unplug patch chords...

void myObj_plug(t_myObj *self, t_symbol *s, int argc, t_atom *argv)
{
    if (argc == 2)
    {

        optional<t_symbol *> plug = atom_getsymbolarg(0, argc, argv);
        optional<short> value = static_cast<short>(atom_getfloatarg(1, argc, argv));
        if (plug && value)
        {
            const char *name = (*plug)->s_name;
            // logpost(self, 3, "value %s", name);
            // logpost(self, 3, "value %i", *value);
            if (strcmp(name, "freq") == 0)
            {
                self->modulations.frequency_patched = *value != 0;
            }
            else if (strcmp(name, "timbre") == 0)
            {
                self->modulations.timbre_patched = *value != 0;
            }
            else if (strcmp(name, "morph") == 0)
            {
                self->modulations.morph_patched = *value != 0;
            }
            else if (strcmp(name, "trig") == 0)
            {
                logpost(self, 3, "%i", *value);
                self->trigger_toggle = *value != 0;
                self->trigger_connected = *value;
                self->modulations.trigger_patched = self->trigger_toggle && self->trigger_connected;
            }
            else if (strcmp(name, "level") == 0)
            {
                self->modulations.level_patched = *value != 0;
            }
        }
    }
}

void myObj_choose_engine(t_myObj *self, t_floatarg e)
{
    self->patch.engine = self->engine = static_cast<int>(e);
}

void myObj_get_engine(t_myObj *self, t_symbol s)
{

    t_atom argv;
    SETFLOAT(&argv, static_cast<float>(self->voice_->active_engine()));
    outlet_anything(self->info_out, gensym("active_engine"), 1, &argv);
}

#pragma mark----- main pots -----
// main pots

void myObj_frequency(t_myObj *self, t_floatarg m)
{
    self->transposition_ = clamp(static_cast<double>(m), -1.0, 1.0);
    logpost(self, 3, "self->transposition_ %f", self->transposition_);
    calc_note(self);
}

void myObj_harmonics(t_myObj *self, t_floatarg h)
{
    self->patch.harmonics = clamp(static_cast<double>(h), 0.0, 1.0);
}

void myObj_timbre(t_myObj *self, t_floatarg t)
{
    self->patch.timbre = clamp(static_cast<double>(t), 0., 1.);
}

void myObj_morph(t_myObj *self, t_floatarg m)
{
    self->patch.morph = clamp(static_cast<double>(m), 0., 1.);
}

// smaller pots
void myObj_timbre_mod_amount(t_myObj *self, t_floatarg m)
{
    self->patch.timbre_modulation_amount = clamp(static_cast<double>(m), -1., 1.);
}

void myObj_freq_mod_amount(t_myObj *self, t_floatarg m)
{
    self->patch.frequency_modulation_amount = clamp(static_cast<double>(m), -1., 1.);
}

void myObj_morph_mod_amount(t_myObj *self, t_floatarg m)
{
    self->patch.morph_modulation_amount = clamp(static_cast<double>(m), -1., 1.);
}

#pragma mark----- hidden parameter -----

// hidden parameters

void myObj_decay(t_myObj *self, t_floatarg m)
{
    self->patch.decay = clamp(static_cast<double>(m), 0., 1.);
}
void myObj_lpg_colour(t_myObj *self, t_floatarg m)
{
    self->patch.lpg_colour = clamp(static_cast<double>(m), 0., 1.);
}

void myObj_octave(t_myObj *self, t_floatarg m)
{
    self->octave_ = clamp(static_cast<double>(m), 0., 1.);
    calc_note(self);
}

// this directly sets the pitch via a midi note
void myObj_note(t_myObj *self, t_floatarg n)
{
    self->patch.note = n;
}

// ---------------------------------------------------- //

static t_int *myObj_perform(t_int *w)
{
    t_myObj *self = (t_myObj *)(w[1]);
    // 8 audio inputs, 2 outputs
    t_sample *trig_input = (t_sample *)(w[8]);
    t_sample *out = (t_sample *)(w[10]);
    t_sample *aux = (t_sample *)(w[11]);

    int vs = (int)(w[12]); // sampleframes
    size_t size = kBlockSize;
    double *out_tmp = self->out_tmp;
    double *aux_tmp = self->aux_tmp;

    // double  pitch_lp_ = 0.; //self->pitch_lp_;
    size_t count = 0;

    int ins = 2;

    double *destination = &self->modulations.engine;

    for (count = 0; count < vs; count += size)
    {

        for (int i = 0; i < 8; i++)
        {
            t_sample *in = (t_sample *)(w[ins + i]);
            destination[i] = in[count];
        }

        if (self->modulations.trigger_patched)
        {
            // calc sum of trigger input
            double vectorsum = 0.0;
#ifdef __APPLE__
            vDSP_sveD(trig_input + count, 1, &vectorsum, size);
#else
            for (int i = 0; i < size; ++i)
                vectorsum += trig_input[i + count];
#endif
            self->modulations.trigger = vectorsum;
        }

        // smooth out pitch changes
        // ONE_POLE(pitch_lp_, self->modulations.note, 0.7);

        // self->modulations.note = pitch_lp_;

        self->voice_->Render(self->patch, self->modulations, out_tmp, aux_tmp, size);
        std::copy(out_tmp, out_tmp + size, out + count);
        std::copy(aux_tmp, aux_tmp + size, aux + count);
    }

    return (w + 13);
}

static void myObj_dsp(t_myObj *self, t_signal **sp)
{

    // self->trigger_connected = 0;
    // self->modulations.trigger_patched = self->trigger_toggle && self->trigger_connected;

    if (sys_getblksize() < kBlockSize)
    {
        pd_error((t_object *)self, "sigvs can't be smaller than %d samples, sorry!", kBlockSize);
        return;
    }

    if (sys_getsr() != self->sr)
    {
        self->sr = sys_getsr();
        kSampleRate = self->sr;
        a0 = (440.0f / 8.0f) / kSampleRate;
    }

    dsp_add(myObj_perform, 12 /* x+inlets+outlets+s_n */,
            self,
            sp[0]->s_vec, // 8 inlets
            sp[1]->s_vec,
            sp[2]->s_vec,
            sp[3]->s_vec,
            sp[4]->s_vec,
            sp[5]->s_vec,
            sp[6]->s_vec,
            sp[7]->s_vec,
            sp[8]->s_vec, // 2 outlets
            sp[9]->s_vec,
            sp[0]->s_n);
}

#pragma mark---- free function ----

void myObj_free(t_myObj *self)
{
    self->voice_->plaits::Voice::~Voice();
    delete self->voice_;

    inlet_free(self->m_in2);
    inlet_free(self->m_in3);
    inlet_free(self->m_in4);
    inlet_free(self->m_in5);
    inlet_free(self->m_in6);
    inlet_free(self->m_trig);
    inlet_free(self->m_level);

    outlet_free(self->m_out);
    outlet_free(self->m_aux);
    outlet_free(self->info_out);
    // delete self->modulator;

    if (self->shared_buffer)
        freebytes(self->shared_buffer, self->shared_buffer_bytes);

    freebytes(self->out_tmp, kBlockSize * sizeof(double));
    freebytes(self->aux_tmp, kBlockSize * sizeof(double));
}

extern "C"
{
    extern void setup_pd0x2emi0x2eplts_tilde(void)
    {
        this_class = class_new(gensym("pd.mi.plts~"),
                               (t_newmethod)myObj_new, (t_method)myObj_free,
                               sizeof(t_myObj), CLASS_DEFAULT, A_GIMME, 0);
        class_addcreator(
            (t_newmethod)myObj_new,
            gensym("mi/plts~"),
            A_GIMME, 0
        );
        if (this_class)
        {
            CLASS_MAINSIGNALIN(this_class, t_myObj, m_f);
            class_addmethod(this_class, (t_method)myObj_dsp, gensym("dsp"), A_CANT, 0);

            // main pots
            class_addmethod(this_class, (t_method)myObj_frequency, gensym("frequency"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_harmonics, gensym("harmonics"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_timbre, gensym("timbre"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_morph, gensym("morph"), A_FLOAT, 0);

            // small pots
            class_addmethod(this_class, (t_method)myObj_morph_mod_amount, gensym("morph_mod"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_timbre_mod_amount, gensym("timbre_mod"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_freq_mod_amount, gensym("freq_mod"), A_FLOAT, 0);

            // hidden parameters
            class_addmethod(this_class, (t_method)myObj_octave, gensym("octave"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_lpg_colour, gensym("lpg_colour"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_decay, gensym("decay"), A_FLOAT, 0);

            class_addmethod(this_class, (t_method)myObj_note, gensym("note"), A_FLOAT, 0);

            class_addmethod(this_class, (t_method)myObj_choose_engine, gensym("engine"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_get_engine, gensym("get_engine"), A_DEFSYMBOL, 0);
            class_addmethod(this_class, (t_method)myObj_plug, gensym("plug"), A_GIMME, 0);
            // class_addmethod(this_class, (t_method)myObj_float, gensym("float"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_info, gensym("info"), A_DEFSYMBOL, 0);

            post("vb.mi.plts~ by volker böhm --> https://vboehm.net");
            post("rewritten for Pd as pd.mi.plts~ by przemysław sanecki --> https://software-materialism.org");
            post("a clone of mutable instruments' 'plaits' module");
        }
    }
}