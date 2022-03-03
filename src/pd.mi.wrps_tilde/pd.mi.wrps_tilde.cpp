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

#include "warps/dsp/modulator.h"
#include "warps/dsp/oscillator.h"
#include "read_inputs.hpp"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <optional>

using std::clamp;

// TODO: work on block size and SR, use libsamplerate for downsampling?
// original SR: 96 kHz, block size: 60

const size_t kBlockSize = 16; // has to stay like that TODO: why?

static t_class *this_class;

struct t_myObj
{
    t_object m_obj; // pd object - always placed in first in the object's struct

    warps::Modulator *modulator;
    warps::ReadInputs *read_inputs;

    t_sample adc_inputs[warps::ADC_LAST];
    short patched[2];
    short easterEgg;
    uint8_t carrier_shape;
    double pre_gain;

    long input_bytes;
    warps::FloatFrame *input;
    long output_bytes;
    warps::FloatFrame *output;

    long count;
    double sr;
    int sigvs;

    t_inlet *m_in2;
    t_inlet *m_level1;
    t_inlet *m_level2;
    t_inlet *m_algo;
    t_inlet *m_timbre;
    t_float f_in2;
    t_float f_level1;
    t_float f_level2;
    t_float f_algo;
    t_float f_timbre;

    t_outlet *m_out;
    t_outlet *m_aux;
    t_float m_f;
};

static void *myObj_new(t_symbol *s, int argc, t_atom *argv)
{
    t_myObj *self = (t_myObj *)pd_new(this_class);
    if (self)
    {
        // six audio inputs
        self->m_in2 = signalinlet_new((t_object *)self, self->f_in2);
        self->m_level1 = signalinlet_new((t_object *)self, self->f_level1); // level 1
        self->m_level2 = signalinlet_new((t_object *)self, self->f_level2); // level 2
        self->m_algo = signalinlet_new((t_object *)self, self->f_algo);     // algo
        self->m_timbre = signalinlet_new((t_object *)self, self->f_timbre); // timbre
        self->m_out = outlet_new((t_object *)self, &s_signal);              // 'out' output
        self->m_aux = outlet_new((t_object *)self, &s_signal);              // 'aux' output

        self->sr = sys_getsr();
        if (self->sr <= 0)
            self->sr = 44100.0;

        // init some params
        self->count = 0;
        self->easterEgg = 0;
        self->patched[0] = self->patched[1] = 0;
        self->carrier_shape = 1;

        self->modulator = new warps::Modulator;
        memset(self->modulator, 0, sizeof(*t_myObj::modulator));
        self->modulator->Init(self->sr);

        self->modulator->mutable_parameters()->note = 110.0f; // (Hz)

        self->read_inputs = new warps::ReadInputs;
        self->read_inputs->Init();

        for (int i = 0; i < warps::ADC_LAST; i++)
            self->adc_inputs[i] = 0.0;

        // TODO: check memory size: should it be warps::kMaxBlockSize?
        self->input_bytes = kBlockSize * sizeof(warps::FloatFrame);
        self->input = (warps::FloatFrame *)getbytes(self->input_bytes);
        self->output_bytes = kBlockSize * sizeof(warps::FloatFrame);
        self->output = (warps::FloatFrame *)getbytes(self->output_bytes);
    }

    return (void *)self;
}

void myObj_info(t_myObj *self)
{
    warps::Parameters *p = self->modulator->mutable_parameters();

    post("Modulator Parameters ----------->");
    post("drive[0]: %f", p->channel_drive[0]);
    post("drive[1]: %f", p->channel_drive[1]);
    post("mod_algo: %f", p->modulation_algorithm);
    post("mod param: %f", p->modulation_parameter);
    post("car_shape: %d", p->carrier_shape);

    post("freqShift_pot: %f", p->frequency_shift_pot);
    post("freqShift_cv: %f", p->frequency_shift_cv);

    post("phaseShift: %f", p->phase_shift);
    post("note: %f", p->note);
}

#pragma mark------ main pods ------

void myObj_modulation_algo(t_myObj *self, float m)
{
    // Selects which signal processing operation is performed on the carrier and modulator.
    m *= 0.125;
    verbose(3, "modulation algo %f", m);
    self->adc_inputs[warps::ADC_ALGORITHM_POT] = clamp(m, 0.0f, 1.0f);
}

void myObj_modulation_timbre(t_myObj *self, float m)
{
    // Controls the intensity of the high harmonics created by cross-modulation
    // (or provides another dimension of tone control for some algorithms).
    verbose(3, "modulation timbre %f", m);
    self->adc_inputs[warps::ADC_PARAMETER_POT] = clamp(m, 0.0f, 1.0f);
}
// oscillator button

void myObj_osc_shape(t_myObj *self, float t)
{
    // Enables the internal oscillator and selects its waveform.
    self->carrier_shape = clamp((int)t, 0, 3);
    verbose(3, "shape %i", self->carrier_shape);
    //    if (!self->easterEgg)
    self->modulator->mutable_parameters()->carrier_shape = self->carrier_shape;
}

#pragma mark--------- small pots ----------

void myObj_level1(t_myObj *self, float m)
{
    // External carrier amplitude or internal oscillator frequency.
    // When the internal oscillator is switched off, this knob controls the amplitude of the carrier, or the amount of amplitude modulation from the channel 1 LEVEL CV input (1). When the internal oscillator is active, this knob controls its frequency.
    verbose(3, "level 1 %f", m);
    self->adc_inputs[warps::ADC_LEVEL_1_POT] = clamp(m, 0.0f, 1.0f);
}

void myObj_level2(t_myObj *self, float m)
{
    // This knob controls the amplitude of the modulator, or the amount of amplitude modulation from the channel 2 LEVEL CV input (2). Past a certain amount of gain, the signal soft clips.
    verbose(3, "level 2 %f", m);
    self->adc_inputs[warps::ADC_LEVEL_2_POT] = clamp(m, 0.0f, 1.0f);
}

#pragma mark-------- other messages ----------

void myObj_freq(t_myObj *self, float n)
{
    // set freq (in Hz) of internal oscillator
    self->modulator->mutable_parameters()->note = clamp(n, 0.0f, 15000.0f);
}

void myObj_bypass(t_myObj *self, float t)
{
    self->modulator->set_bypass((int)t != 0);
}

void myObj_easter_egg(t_myObj *self, float t)
{

    //    if((int)t != 0) {
    //        self->easterEgg = true;
    //        self->modulator->mutable_parameters()->carrier_shape = 1;
    //    }
    //    else {
    //        self->easterEgg = false;
    //        self->modulator->mutable_parameters()->carrier_shape = self->carrier_shape;
    //    }
    self->easterEgg = ((int)t != 0);
    self->modulator->set_easter_egg(self->easterEgg);
}

static t_int *myObj_perform(t_int *w)
{
    t_myObj *self = (t_myObj *)(w[1]);
    t_sample *in1 = (t_sample *)(w[2]);
    t_sample *in2 = (t_sample *)(w[3]);
    // t_sample* in3 = (t_sample *)(w[4]); // level 1
    // t_sample* in4 = (t_sample *)(w[5]); // level 2
    // t_sample* in5 = (t_sample *)(w[6]);
    // t_sample* in6 = (t_sample *)(w[7]);
    t_sample *out = (t_sample *)(w[8]);
    t_sample *aux = (t_sample *)(w[9]);
    int vs = (int)(w[10]);

    warps::FloatFrame *input = self->input;
    warps::FloatFrame *output = self->output;

    long count = self->count;
    t_sample *adc_inputs = self->adc_inputs;

    for (int idx = 0; idx < 4; idx++)
    {
        adc_inputs[idx] = ((t_sample *)w[idx + 4])[0];
    }
    // adc_inputs[0] = in3[0];
    // adc_inputs[1] = in4[0];
    // adc_inputs[2] = in5[0];
    // adc_inputs[3] = in6[0];
    self->read_inputs->Read(self->modulator->mutable_parameters(), adc_inputs, self->patched);

    for (int i = 0; i < vs; i++)
    {
        input[count].l = in1[i];
        input[count].r = in2[i];

        count++;
        if (count >= kBlockSize)
        {
            self->modulator->Processf(input, output, kBlockSize);
            count = 0;
        }

        out[i] = output[count].l;
        aux[i] = output[count].r;
    }

    return (w + 11);
}

static void myObj_dsp(t_myObj *self, t_signal **sp)
{
    t_float samplerate = sys_getsr();
    if (samplerate != self->sr)
    {
        self->sr = samplerate;
        self->modulator->Init(self->sr);
    }
    dsp_add(myObj_perform, 10 /* x+inlets+outlets+s_n */,
            self,
            sp[0]->s_vec, // 6 inlets
            sp[1]->s_vec,
            sp[2]->s_vec,
            sp[3]->s_vec,
            sp[4]->s_vec,
            sp[5]->s_vec,
            sp[6]->s_vec, // 2 outlets
            sp[7]->s_vec,
            sp[0]->s_n);
}

// plug / unplug patch chords...

void myObj_plug(t_myObj *self, t_symbol *s, int argc, t_atom *argv)
{
    if (argc == 2)
    {
        std::optional<t_symbol *> plug = atom_getsymbolarg(0, argc, argv);
        std::optional<short> value = static_cast<short>(atom_getfloatarg(1, argc, argv));
        if (plug && value)
        {
            const char *name = (*plug)->s_name;
            bool plugged = *value != 0.0;
            logpost(self, 3, "plugged %s, %i", name, plugged);
            if (strcmp(name, "level1") == 0)
            {
                self->patched[0] = plugged;
            }
            else if (strcmp(name, "level2") == 0)
            {
                self->patched[1] = plugged;
            }
        }
    }
}

#pragma mark---- free function ----

static void myObj_free(t_myObj *self)
{
    inlet_free(self->m_in2);
    inlet_free(self->m_level1);
    inlet_free(self->m_level2);
    inlet_free(self->m_algo);
    inlet_free(self->m_timbre);
    outlet_free(self->m_out);
    outlet_free(self->m_aux);
    delete self->modulator;

    freebytes(self->input, self->input_bytes);
    freebytes(self->output, self->output_bytes);
}

extern "C"
{
    extern void setup_pd0x2emi0x2ewrps_tilde(void)
    {
        this_class = class_new(gensym("pd.mi.wrps~"),
                               (t_newmethod)myObj_new, (t_method)myObj_free,
                               sizeof(t_myObj), CLASS_DEFAULT, A_GIMME, 0);
        if (this_class)
        {
            CLASS_MAINSIGNALIN(this_class, t_myObj, m_f);
            class_addmethod(this_class, (t_method)myObj_dsp, gensym("dsp"), A_GIMME);
            // main pots
            class_addmethod(this_class, (t_method)myObj_modulation_algo, gensym("algo"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_modulation_timbre, gensym("timbre"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_osc_shape, gensym("osc_shape"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_level1, gensym("level1"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_level2, gensym("level2"), A_FLOAT, 0);

            class_addmethod(this_class, (t_method)myObj_freq, gensym("freq"), A_FLOAT, 0);

            class_addmethod(this_class, (t_method)myObj_bypass, gensym("bypass"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_easter_egg, gensym("easteregg"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_plug, gensym("plug"), A_GIMME, 0);
            class_addmethod(this_class, (t_method)myObj_info, gensym("info"), A_GIMME, 0);

            post("vb.mi.wrps~ by volker böhm --> https://vboehm.net");
            post("rewritten for Pd as pd.mi.wrps~ by przemysław sanecki --> https://software-materialism.org");
            post("a clone of mutable instruments' 'warps' module");
        }
    }
}