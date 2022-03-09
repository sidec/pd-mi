//
// Copyright 2020 Volker Böhm.
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

// a clone of mutable instruments' 'Tides(2)' module for maxmsp
// by volker böhm, july 2020, https://vboehm.net

// Original code by Émilie Gillet, https://mutable-instruments.net/

#include <m_pd.h>

#include "tides2/poly_slope_generator.h"
#include "tides2/ramp_extractor.h"

#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <optional>

using std::clamp;
using std::optional;

const size_t kAudioBlockSize = 8; // sig vs can't be smaller than this!
const size_t kNumOutputs = 4;

static t_class *this_class = nullptr;

struct t_myObj
{
    t_object obj;

    tides::PolySlopeGenerator poly_slope_generator;
    tides::RampExtractor ramp_extractor;

    tides::PolySlopeGenerator::OutputSample out[kAudioBlockSize];
    stmlib::GateFlags no_gate[kAudioBlockSize];
    stmlib::GateFlags gate_input[kAudioBlockSize];
    stmlib::GateFlags clock_input[kAudioBlockSize];
    stmlib::GateFlags previous_flags_[2 + 1]; // two inlets, TODO: why + 1
    tides::Ratio r_;

    float ramp[kAudioBlockSize];

    tides::OutputMode output_mode;
    tides::OutputMode previous_output_mode;
    tides::RampMode ramp_mode;
    tides::Range range;

    float frequency, freq_lp;
    float shape, shape_lp;
    float slope, slope_lp;
    float smoothness, smooth_lp;
    float shift, shift_lp;

    bool must_reset_ramp_extractor;
    bool trig_connected;
    bool clock_connected;
    bool use_trigger;
    bool use_clock;

    float sr;
    float r_sr;
    long sigvs;

    t_inlet *m_shape;
    t_inlet *m_slope;
    t_inlet *m_smooth;
    t_inlet *m_shift;
    t_inlet *m_trig;
    t_inlet *m_clock;
    t_float f_shape;
    t_float f_slope;
    t_float f_smooth;
    t_float f_shift;
    t_float f_trig;
    t_float f_clock;

    t_outlet *m_out0;
    t_outlet *m_out1;
    t_outlet *m_out2;
    t_outlet *m_out3;
    t_float m_f;
};

static tides::Ratio kRatios[19] = {
    {0.0625f, 16},
    {0.125f, 8},
    {0.1666666f, 6},
    {0.25f, 4},
    {0.3333333f, 3},
    {0.5f, 2},
    {0.6666666f, 3},
    {0.75f, 4},
    {0.8f, 5},
    {1, 1},
    {1.25f, 4},
    {1.3333333f, 3},
    {1.5f, 2},
    {2.0f, 1},
    {3.0f, 1},
    {4.0f, 1},
    {6.0f, 1},
    {8.0f, 1},
    {16.0f, 1},
};

void *myObj_new(t_symbol *s, int argc, t_atom *argv)
{
    t_myObj *self = (t_myObj *)pd_new(this_class);

    if (self)
    {
        // 7 audio inputs
        self->m_shape = signalinlet_new((t_object *)self, self->f_shape);
        self->m_slope = signalinlet_new((t_object *)self, self->f_slope);
        self->m_smooth = signalinlet_new((t_object *)self, self->f_smooth);
        self->m_shift = signalinlet_new((t_object *)self, self->f_shift);
        self->m_trig = signalinlet_new((t_object *)self, self->f_trig);
        self->m_clock = signalinlet_new((t_object *)self, self->f_clock);
        // 4 audio outs
        self->m_out0 = outlet_new((t_object *)self, &s_signal);
        self->m_out1 = outlet_new((t_object *)self, &s_signal);
        self->m_out2 = outlet_new((t_object *)self, &s_signal);
        self->m_out3 = outlet_new((t_object *)self, &s_signal);

        self->sigvs = sys_getblksize();

        if (self->sigvs < kAudioBlockSize)
        {
            pd_error((t_object *)self,
                     "sigvs can't be smaller than %d samples\n", kAudioBlockSize);
            delete self;
            self = NULL;
            return self;
        }

        self->sr = sys_getsr();
        self->r_sr = 1.f / self->sr;

        self->poly_slope_generator.Init();
        self->ramp_extractor.Init(self->sr, 40.0f * self->r_sr);

        std::fill(&self->no_gate[0], &self->no_gate[kAudioBlockSize], stmlib::GATE_FLAG_LOW);
        std::fill(&self->clock_input[0], &self->clock_input[kAudioBlockSize], stmlib::GATE_FLAG_LOW);
        std::fill(&self->ramp[0], &self->ramp[kAudioBlockSize], 0.f);

        self->output_mode = tides::OUTPUT_MODE_GATES;
        self->previous_output_mode = tides::OUTPUT_MODE_GATES;
        self->ramp_mode = tides::RAMP_MODE_LOOPING;
        self->range = tides::RANGE_CONTROL;

        self->previous_flags_[0] = stmlib::GATE_FLAG_LOW;
        self->previous_flags_[1] = stmlib::GATE_FLAG_LOW;

        self->use_trigger = false;
        self->use_clock = false;
        self->must_reset_ramp_extractor = false;

        self->frequency = 1.f;
        self->shape = 0.5f;
        self->slope = 0.5f;
        self->smoothness = 0.5f;
        self->shift = 0.3f;

        self->freq_lp = self->shape_lp = self->slope_lp = self->smooth_lp = self->shift_lp = 0.f;

        self->r_.ratio = 1.0f;
        self->r_.q = 1;

        // process attributes
        // attr_args_process(self, argc, argv);
        int argnum = 0;
        while (argc > 0)
        {
            if (argv->a_type == A_SYMBOL)
            {
                t_symbol *curarg = atom_getsymbolarg(0, argc, argv);
                if (strcmp(curarg->s_name, "@output_mode") == 0)
                {
                    if (argc >= 2)
                    {
                        t_float argval = atom_getfloatarg(1, argc, argv);
                        long _m = clamp((int)argval, 0, 3);
                        self->output_mode = tides::OutputMode(_m);
                        argc -= 2;
                        argv += 2;
                    }
                }
                else if (strcmp(curarg->s_name, "@ramp_mode") == 0)
                {
                    if (argc >= 2)
                    {
                        t_float argval = atom_getfloatarg(1, argc, argv);
                        long _m = clamp((int)argval, 0, 2);
                        self->ramp_mode = tides::RampMode(_m);
                        argc -= 2;
                        argv += 2;
                    }
                }
                else if (strcmp(curarg->s_name, "@range") == 0)
                {
                    if (argc >= 2)
                    {
                        t_float argval = atom_getfloatarg(1, argc, argv);
                        long _m = clamp((int)argval, 0, 1);
                        self->range = tides::Range(_m);
                        argc -= 2;
                        argv += 2;
                    }
                }
                else if (strcmp(curarg->s_name, "@use_trigger") == 0)
                {
                    if (argc >= 2)
                    {
                        t_float argval = atom_getfloatarg(1, argc, argv);
                        self->use_trigger = (int)argval != 0;
                        self->trig_connected = self->use_trigger;
                        argc -= 2;
                        argv += 2;
                    }
                }
                else if (strcmp(curarg->s_name, "@use_clock") == 0)
                {
                    if (argc >= 2)
                    {
                        t_float argval = atom_getfloatarg(1, argc, argv);
                        self->use_clock = (int)argval != 0;
                        self->clock_connected = self->use_clock;
                        argc -= 2;
                        argv += 2;
                    }
                }
            }
        }
    }
    else
    {
        delete self;
        self = NULL;
    }

    return (void *)self;
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
            if (strcmp(name, "trigger") == 0)
            {
                self->use_trigger = (*value != 0);
                self->trig_connected = self->use_trigger;
                logpost(self, 3, "use trig %i", self->use_trigger);
            }
            else if (strcmp(name, "clock") == 0)
            {
                self->use_clock = (*value != 0);
                self->clock_connected = self->use_clock;
                logpost(self, 3, "use clock %i", self->use_clock);
            }
        }
    }
}

#pragma mark-------- general pots -----------

void myObj_freq(t_myObj *self, t_float m)
{
    verbose(3, "frequency %f", m);
    self->frequency = m;
}

void myObj_shape(t_myObj *self, t_float m)
{
    verbose(3, "shape %f", m);
    self->shape = m;
}

void myObj_slope(t_myObj *self, t_float m)
{
    verbose(3, "slope %f", m);
    self->slope = m;
}

void myObj_smooth(t_myObj *self, t_float m)
{
    verbose(3, "smoothness %f", m);
    self->smoothness = m;
}

void myObj_shift(t_myObj *self, t_float m)
{
    verbose(3, "shift %f", m);
    self->shift = m;
}

void myObj_ratio(t_myObj *self, t_float m)
{
    CONSTRAIN(m, 0, 18);
    self->r_ = kRatios[(int)m];
    //    post((t_object*)self, "ratio[%d]: ratio: %f -- q: %d", m, self->r_.ratio, self->r_.q);
}

void output_mode_setter(t_myObj *self, t_float m)
{
    long _m = clamp((int)m, 0, 3);
    verbose(3, "output mode %d", _m);
    self->output_mode = tides::OutputMode(_m);
    if (self->output_mode != self->previous_output_mode)
    {
        self->poly_slope_generator.Reset();
        self->previous_output_mode = self->output_mode;
    }
}

void ramp_mode_setter(t_myObj *self, t_float m)
{
    long _m = clamp((int)m, 0, 2);
    self->ramp_mode = tides::RampMode(_m);
}

void range_setter(t_myObj *self, t_float m)
{
    long _m = clamp((int)m, 0, 1);
    if (_m != 0)
    {
        self->range = tides::RANGE_AUDIO;
    }
    else
    {
        self->range = tides::RANGE_CONTROL;
    }
}

#pragma mark-------- DSP Loop ----------

static t_int *myObj_perform(t_int *w)
{
    t_myObj *self = (t_myObj *)(w[1]);
    // 7 audio inputs, 4 outputs

    t_sample *freq_in = (t_sample *)w[2];
    t_sample *shape_in = (t_sample *)w[3];
    t_sample *slope_in = (t_sample *)w[4];
    t_sample *smooth_in = (t_sample *)w[5];
    t_sample *shift_in = (t_sample *)w[6];

    t_sample *trig_in = (t_sample *)w[7];  // trigger signal input
    t_sample *clock_in = (t_sample *)w[8]; // clock signal input

    int vs = (int)(w[13]); // sampleframes

    tides::RampExtractor *ramp_extractor = &self->ramp_extractor;
    tides::PolySlopeGenerator::OutputSample *out = self->out;
    float *ramp = self->ramp;

    stmlib::GateFlags *clock_input = self->clock_input;
    stmlib::GateFlags *gate_flags = self->no_gate;
    stmlib::GateFlags *previous_flags = self->previous_flags_;

    bool must_reset_ramp_extractor = self->must_reset_ramp_extractor;
    bool use_clock = self->use_clock;
    bool use_trigger = self->use_trigger;
    bool clock_connected = self->clock_connected;
    bool trig_connected = self->trig_connected;

    tides::OutputMode output_mode = self->output_mode;
    tides::RampMode ramp_mode = self->ramp_mode;
    tides::Range range = self->range;

    float frequency, shape, slope, shift, smoothness;
    float freq_knob = self->frequency;
    float shape_knob = self->shape;
    float slope_knob = self->slope;
    float shift_knob = self->shift;
    float smoothness_knob = self->smoothness;

    float freq_lp = self->freq_lp;
    float shape_lp = self->shape_lp;
    float slope_lp = self->slope_lp;
    float shift_lp = self->shift_lp;
    float smooth_lp = self->smooth_lp;

    float r_sr = self->r_sr;

    for (int count = 0; count < vs; count += kAudioBlockSize)
    {

        // check for gate/trigger input
        if (use_trigger && trig_connected)
        {

            gate_flags = self->gate_input;
            for (int i = 0; i < kAudioBlockSize; ++i)
            {
                bool trig = trig_in[i + count] > 0.01;
                previous_flags[0] = stmlib::ExtractGateFlags(previous_flags[0], trig);
                gate_flags[i] = previous_flags[0];
            }
        }

        if (use_clock && clock_connected)
        {

            if (must_reset_ramp_extractor)
            {
                ramp_extractor->Reset();
            }

            for (int i = 0; i < kAudioBlockSize; ++i)
            {
                bool trig = clock_in[i + count] > 0.01;
                previous_flags[1] = stmlib::ExtractGateFlags(previous_flags[1], trig);
                clock_input[i] = previous_flags[1];
            }

            frequency = ramp_extractor->Process(range,
                                                range == tides::RANGE_AUDIO && ramp_mode == tides::RAMP_MODE_AR,
                                                self->r_,
                                                clock_input,
                                                ramp,
                                                kAudioBlockSize);

            must_reset_ramp_extractor = false;
        }
        else
        {
            frequency = (freq_in[count] + freq_knob) * r_sr;
            CONSTRAIN(frequency, 0.f, 0.4f);
            // no filtering for now
            //            ONE_POLE(freq_lp, frequency, 0.3f);
            //            frequency = freq_lp;
            must_reset_ramp_extractor = true;
        }

        // parameter inputs
        shape = shape_knob + (float)shape_in[count];
        CONSTRAIN(shape, 0.f, 1.f);
        ONE_POLE(shape_lp, shape, 0.1f);
        slope = slope_knob + (float)slope_in[count];
        CONSTRAIN(slope, 0.f, 1.f);
        ONE_POLE(slope_lp, slope, 0.1f);
        smoothness = smoothness_knob + (float)smooth_in[count];
        CONSTRAIN(smoothness, 0.f, 1.f);
        ONE_POLE(smooth_lp, smoothness, 0.1f);
        shift = shift_knob + shift_in[count];
        CONSTRAIN(shift, 0.f, 1.f);
        ONE_POLE(shift_lp, shift, 0.1f);

        self->poly_slope_generator.Render(ramp_mode,
                                          output_mode,
                                          range,
                                          frequency, slope_lp, shape_lp, smooth_lp, shift_lp,
                                          gate_flags,
                                          !use_trigger && use_clock ? ramp : NULL,
                                          out, kAudioBlockSize);

        for (int i = 0; i < kAudioBlockSize; ++i)
        {
            for (int j = 0; j < kNumOutputs; ++j)
            {
                ((t_sample *)w[j + 9])[i + count] = out[i].channel[j] * 0.1f;
            }
        }
    }

    self->freq_lp = freq_lp;
    self->shape_lp = shape_lp;
    self->shift_lp = shift_lp;
    self->slope_lp = slope_lp;
    self->smooth_lp = smooth_lp;

    self->must_reset_ramp_extractor = must_reset_ramp_extractor;

    return (w + 14);
}

void myObj_dsp(t_myObj *self, t_signal **sp)
{
    // is a signal connected to the trigger/clock input?

    if (sys_getblksize() < kAudioBlockSize)
    {
        pd_error((t_object *)self, "sigvs can't be smaller than %d samples, sorry!", kAudioBlockSize);
        return;
    }
    if (sys_getsr() != self->sr)
    {
        self->sr = sys_getsr();
        self->r_sr = 1.0f / self->sr;
    }

    dsp_add(myObj_perform, 13 /* x+inlets+outlets+s_n */,
            self,
            sp[0]->s_vec, // 7 inlets
            sp[1]->s_vec,
            sp[2]->s_vec,
            sp[3]->s_vec,
            sp[4]->s_vec,
            sp[5]->s_vec,
            sp[6]->s_vec,
            sp[7]->s_vec, // 4 outlets
            sp[8]->s_vec,
            sp[9]->s_vec,
            sp[10]->s_vec,
            sp[0]->s_n);
}

void myObj_free(t_myObj *self)
{
    self->poly_slope_generator.~PolySlopeGenerator();
    self->ramp_extractor.~RampExtractor();

    inlet_free(self->m_shape);
    inlet_free(self->m_slope);
    inlet_free(self->m_smooth);
    inlet_free(self->m_shift);
    inlet_free(self->m_trig);
    inlet_free(self->m_clock);

    outlet_free(self->m_out0);
    outlet_free(self->m_out1);
    outlet_free(self->m_out2);
    outlet_free(self->m_out3);
}

extern "C"
{
    extern void setup_pd0x2emi0x2etds_tilde(void)
    {
        this_class = class_new(gensym("pd.mi.tds~"),
                               (t_newmethod)myObj_new, (t_method)myObj_free,
                               sizeof(t_myObj), CLASS_DEFAULT, A_GIMME, 0);
        class_addcreator(
            (t_newmethod)myObj_new,
            gensym("mi/tds~"),
            A_GIMME, 0);

        if (this_class)
        {
            CLASS_MAINSIGNALIN(this_class, t_myObj, m_f);
            // class_addmethod(this_class, (t_method)myObj_assist, gensym("assist"), A_CANT, 0);
            class_addmethod(this_class, (t_method)myObj_dsp, gensym("dsp"), A_CANT, 0);

            // class_addmethod(this_class, (t_method)myObj_int, gensym("int"), A_LONG, 0);
            // class_addmethod(this_class, (t_method)myObj_float, gensym("float"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_freq, gensym("freq"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_shape, gensym("shape"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_shift, gensym("shift"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_slope, gensym("slope"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_smooth, gensym("smooth"), A_FLOAT, 0);
            class_addmethod(this_class, (t_method)myObj_ratio, gensym("ratio"), A_FLOAT, 0);

            // ATTRIBUTES ..............
            // output mode
            // "GATE AMPLITUDE PHASE FREQUENCY"
            class_addmethod(this_class, (t_method)output_mode_setter, gensym("output_mode"), A_FLOAT, 0);
            logpost(this_class, 3, "pd.mi.tds~ @output mode: 0: GATE, 1: AMPLITUDE, 2: PHASE, 3: FREQUENCY");
            // ramp mode
            // "AD LOOPING AR"
            class_addmethod(this_class, (t_method)ramp_mode_setter, gensym("ramp_mode"), A_FLOAT, 0);
            logpost(this_class, 3, "pd.mi.tds~ @ramp mode: 0: AD, 1: LOOPING, 2: AR");
            // range
            // "CONTROL AUDIO"
            class_addmethod(this_class, (t_method)range_setter, gensym("range"), A_FLOAT, 0);
            logpost(this_class, 3, "pd.mi.tds~ @range: 0: CONTROL, 1: AUDIO");

            class_addmethod(this_class, (t_method)myObj_plug, gensym("plug"), A_GIMME, 0);
            logpost(this_class, 3, "pd.mi.tds~ @use_trigger: 0|1");
            logpost(this_class, 3, "pd.mi.tds~ @use_clock: 0|1");

            logpost(this_class, 3, "pd.mi.tds~ by Przemysław Sanecki -- https://software-materialism.org");
            logpost(this_class, 3, "based on vb.mi.tds~ by Volker Böhm -- https://vboehm.net");
            logpost(this_class, 3, "based on mutable instruments' 'tides' module");
        }
    }
}
