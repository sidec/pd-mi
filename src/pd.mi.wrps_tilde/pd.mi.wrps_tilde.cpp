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

#include <cstring>

using std::clamp;

const size_t kBlockSize = 16;       // has to stay like that TODO: why?

static t_class *pd_mi_wrps_tilde_class;

typedef struct _pd_mi_wrps_tilde
{
    t_object m_obj; // pd object - always placed in first in the object's struct

    warps::Modulator *modulator;
    warps::ReadInputs *read_inputs;

    t_sample adc_inputs[warps::ADC_LAST];
    short patched[2];
    short easterEgg;
    uint8_t carrier_shape;
    double pre_gain;

    
    size_t input_bytes;
    warps::FloatFrame *input;
    size_t output_bytes;
    warps::FloatFrame *output;

    long count;
    t_float sr;
    int sigvs;

    t_outlet *m_out;
    t_outlet *m_aux;
    // doesn't work this way
    // t_float patched_1; 
    // t_float patched_2;
    t_float m_f;

} t_pd_mi_wrps_tilde;

static void* pd_mi_wrps_tilde_new(t_symbol* s, int argc, t_atom *argv)
{
    t_pd_mi_wrps_tilde *x = (t_pd_mi_wrps_tilde*)pd_new(pd_mi_wrps_tilde_class);
    if(x)
    {
        // six audio inputs
        signalinlet_new((t_object *)x, 0);
        signalinlet_new((t_object *)x, 0/*x->patched_1*/); // level 1 
        signalinlet_new((t_object *)x, 0/*x->patched_2*/); // level 2
        signalinlet_new((t_object *)x, 0); // algo 
        signalinlet_new((t_object *)x, 0); // timbre 

        x->m_out = outlet_new((t_object *)x, &s_signal); // 'out' output
        x->m_aux = outlet_new((t_object *)x, &s_signal); // 'aux' output
    
        x->sr = sys_getsr();
        if(x->sr <= 0)
            x->sr = 44100.0;
        
        // init some params
        x->count = 0;
        x->easterEgg = 0;
        x->patched[0] = x->patched[1] = 0;
        x->carrier_shape = 1;
        
        x->modulator = new warps::Modulator;
        memset(x->modulator, 0, sizeof(*t_pd_mi_wrps_tilde::modulator));
        x->modulator->Init(x->sr);
        
        x->modulator->mutable_parameters()->note = 110.0f; // (Hz)
        
        x->read_inputs = new warps::ReadInputs;
        x->read_inputs->Init();

        
        for(int i=0; i<warps::ADC_LAST; i++)
            x->adc_inputs[i] = 0.0;

        // too risky to have it adjustable to Pd block size?
	    // kBlockSize = sys_getblksize() > warps::kMaxBlockSize 
            // ? warps::kMaxBlockSize 
            // : sys_getblksize();
        // TODO: check memory size: should it be warps::kMaxBlockSize?
        x->input_bytes = kBlockSize*sizeof(warps::FloatFrame);
        x->input = (warps::FloatFrame*)getbytes(x->input_bytes);
        x->output_bytes = kBlockSize*sizeof(warps::FloatFrame);
        x->output = (warps::FloatFrame*)getbytes(x->output_bytes);

    }
    
    // return (x);
    return (void*)x;
}

static void pd_mi_wrps_tilde_free(t_pd_mi_wrps_tilde* x)
{
    outlet_free(x->m_out);
    outlet_free(x->m_aux);
    delete x->modulator;

    freebytes(x->input, x->input_bytes);
    freebytes(x->output, x->output_bytes);
}

static t_int *pd_mi_wrps_tilde_perform(t_int *w)
{
    t_pd_mi_wrps_tilde *x = (t_pd_mi_wrps_tilde *)(w[1]);
    t_sample* in1 = (t_sample *)(w[2]);
    t_sample* in2 = (t_sample *)(w[3]);
    t_sample* in3 = (t_sample *)(w[4]); // level 1
    t_sample* in4 = (t_sample *)(w[5]); // level 2
    t_sample* in5 = (t_sample *)(w[6]);
    t_sample* in6 = (t_sample *)(w[7]);
    t_sample* out = (t_sample *)(w[8]);
    t_sample* aux = (t_sample *)(w[9]);
    int vs = (int)(w[10]);

    warps::FloatFrame  *input = x->input;
    warps::FloatFrame  *output = x->output;

    long    count = x->count;
    t_sample *adc_inputs = x->adc_inputs;

    // won't work, use patched1/2 method handler
    // x->patched[0] = x->patched_1 != 0.0;
    // x->patched[1] = x->patched_2 != 0.0;

    adc_inputs[0] = in3[0];
    adc_inputs[1] = in4[0];
    adc_inputs[2] = in5[0];
    adc_inputs[3] = in6[0];
    x->read_inputs->Read(x->modulator->mutable_parameters(), adc_inputs, x->patched);

    for (int i = 0; i < vs; i++) {
        input[count].l = in1[i];
        input[count].r = in2[i];

        count++;
        if (count >= kBlockSize)
        {
            x->modulator->Processf(input, output, kBlockSize);
            count = 0;
        }

        out[i] = output[count].l;
        aux[i] = output[count].r;
    }

    return (w+11);
}

static void pd_mi_wrps_tilde_dsp(t_pd_mi_wrps_tilde* x, t_signal **sp)
{
    t_float samplerate = sys_getsr();
    if (samplerate != x->sr)
    {
        x->sr = samplerate;
        x->modulator->Init(x->sr);
    }
    dsp_add(pd_mi_wrps_tilde_perform, 10 /* x+inlets+outlets+s_n */,
            x,
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

// ------ main pods ------
void pd_mi_wrps_tilde_modulation_algo(t_pd_mi_wrps_tilde* x, float m) {
    // Selects which signal processing operation is performed on the carrier and modulator.
    m *= 0.125;
    verbose(3,"modulation algo %f", m);
    x->adc_inputs[warps::ADC_ALGORITHM_POT] = clamp(m, 0.0f, 1.0f);
}

void pd_mi_wrps_tilde_modulation_timbre(t_pd_mi_wrps_tilde* x, float m) {
    // Controls the intensity of the high harmonics created by cross-modulation
    // (or provides another dimension of tone control for some algorithms).
    verbose(3, "modulation timbre %f", m);
    x->adc_inputs[warps::ADC_PARAMETER_POT] = clamp(m, 0.0f, 1.0f);
}
// oscillator button

void pd_mi_wrps_tilde_osc_shape(t_pd_mi_wrps_tilde* x, float t) {
    // Enables the internal oscillator and selects its waveform.
    x->carrier_shape = clamp((int)t, 0, 3);
    verbose(3,"shape %i", x->carrier_shape);
//    if (!x->easterEgg)
        x->modulator->mutable_parameters()->carrier_shape = x->carrier_shape;
}

// --------- small pots ----------

void pd_mi_wrps_tilde_level1(t_pd_mi_wrps_tilde* x, float m) {
    // External carrier amplitude or internal oscillator frequency.
    // When the internal oscillator is switched off, this knob controls the amplitude of the carrier, or the amount of amplitude modulation from the channel 1 LEVEL CV input (1). When the internal oscillator is active, this knob controls its frequency.
    verbose(3,"level 1 %f", m);
    x->adc_inputs[warps::ADC_LEVEL_1_POT] = clamp(m, 0.0f, 1.0f);
}

void pd_mi_wrps_tilde_level2(t_pd_mi_wrps_tilde* x, float m) {
    // This knob controls the amplitude of the modulator, or the amount of amplitude modulation from the channel 2 LEVEL CV input (2). Past a certain amount of gain, the signal soft clips.
    verbose(3,"level 2 %f", m);
    x->adc_inputs[warps::ADC_LEVEL_2_POT] = clamp(m, 0.0f, 1.0f);
}

//  -------- other messages ----------

void pd_mi_wrps_tilde_freq(t_pd_mi_wrps_tilde* x, float n) {
    // set freq (in Hz) of internal oscillator
    x->modulator->mutable_parameters()->note = clamp(n, 0.0f, 15000.0f);
}


void pd_mi_wrps_tilde_bypass(t_pd_mi_wrps_tilde* x, float t) {
    x->modulator->set_bypass((int)t != 0);
}

void pd_mi_wrps_tilde_easter_egg(t_pd_mi_wrps_tilde* x, float t) {
    
//    if((int)t != 0) {
//        x->easterEgg = true;
//        x->modulator->mutable_parameters()->carrier_shape = 1;
//    }
//    else {
//        x->easterEgg = false;
//        x->modulator->mutable_parameters()->carrier_shape = x->carrier_shape;
//    }
    x->easterEgg = ( (int)t != 0 );
    x->modulator->set_easter_egg(x->easterEgg);
}

void pd_mi_wrps_tilde_info(t_pd_mi_wrps_tilde *x)
{
    warps::Parameters *p = x->modulator->mutable_parameters();

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

// plug / unplug patch chords...

void pd_mi_wrps_tilde_patched1(t_pd_mi_wrps_tilde* x, float t) {
    x->patched[0] = t > 0.0;
    verbose(3,"patched 1 %i", x->patched[0]);
}
void pd_mi_wrps_tilde_patched2(t_pd_mi_wrps_tilde* x, float t) {
    x->patched[1] = t > 0.0;
    verbose(3,"patched 2 %i", t);
}

// Note in c++ you need to wrap the setup method in an extern "C" statement.
extern "C" {
    extern void setup_pd0x2emi0x2ewrps_tilde(void) {
        t_class* c = class_new(gensym("pd.mi.wrps~"),
                               (t_newmethod)pd_mi_wrps_tilde_new, (t_method)pd_mi_wrps_tilde_free,
                               sizeof(t_pd_mi_wrps_tilde), CLASS_DEFAULT, A_GIMME, 0);
        if(c)
        {
            CLASS_MAINSIGNALIN(c, t_pd_mi_wrps_tilde, m_f);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_dsp, gensym("dsp"), A_GIMME);
            // main pots
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_modulation_algo, gensym("algo"), A_FLOAT, 0);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_modulation_timbre, gensym("timbre"), A_FLOAT, 0);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_osc_shape, gensym("osc_shape"), A_FLOAT, 0);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_level1, gensym("level1"), A_FLOAT, 0);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_level2, gensym("level2"), A_FLOAT, 0);

            class_addmethod(c, (t_method)pd_mi_wrps_tilde_freq, gensym("freq"), A_FLOAT, 0);
            
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_bypass, gensym("bypass"), A_FLOAT, 0);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_easter_egg, gensym("easteregg"),A_FLOAT, 0);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_patched1, gensym("patched1"),A_FLOAT, 0);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_patched2, gensym("patched2"),A_FLOAT, 0);
            class_addmethod(c, (t_method)pd_mi_wrps_tilde_info, gensym("info"), A_GIMME, 0);

            post("vb.mi.wrps~ by volker böhm --> https://vboehm.net");
            post("rewritten for Pd as pd.mi.wrps~ by przemysław sanecki --> https://software-materialism.org");
            post("a clone of mutable instruments' 'warps' module");

        }
        pd_mi_wrps_tilde_class = c;
    }
}