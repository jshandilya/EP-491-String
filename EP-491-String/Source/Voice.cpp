/*
  ==============================================================================

    Voice.cpp
    Created: 10 Apr 2023 10:16:26pm
    Author:  Jahan Shandilya

  ==============================================================================
*/

#include "Voice.h"

void Voice::startNote(int midiNote, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition)
{
    bufIdx = 0;
    
    for (int i = 0; i < bufferSize; i++)
    {
        excitation[i] = 0;
        lowpassDelayBuffer[i] = 0;
        allpassTuningBuffer[i] = 0;
        lowpassDelayBuffer[i] = 0;
    }
    
    if (midiNote <= 84)
    {
        calculatePitchInfo (midiNote, currentPitchWheelPosition);
        
        createExcitation (velocity);
        
        playing = true;
        tailoff = false;
    }
    
    else
    {
        playing = false;
    }
    

}

void Voice::stopNote(float velocity, bool allowTailOff)
{
    if (allowTailOff)
    {
        tailoff = true;
        lastTailoffIdx = (bufIdx + (int)(mT60 * getSampleRate())) % bufferSize;
    }

    else
    {
        playing = false;
        clearCurrentNote();
    }
}

void Voice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    auto wpL = outputBuffer.getWritePointer(0);
    auto wpR = outputBuffer.getWritePointer(1);

    for (int i = startSample; playing && i < startSample + numSamples; i++)
    {
        int lowpassIdx1 = (bufIdx + bufferSize - period)       % bufferSize;
        int lowpassIdx2 = (bufIdx + bufferSize - (period + 1)) % bufferSize;

        float rhoToUse = tailoff ? rhoTailoff : mRho;
        float plucked = excitation[bufIdx] + rhoToUse * ((1 - mS) * lowpassDelayBuffer[lowpassIdx1] + mS * lowpassDelayBuffer[lowpassIdx2]);
        
        excitation[bufIdx] = 0;

        lowpassDelayBuffer[bufIdx] = plucked;

        int lastSampleIdx = (bufIdx + bufferSize - 1) % bufferSize;
        plucked = allpassTuningC * plucked + lowpassDelayBuffer[lastSampleIdx]
            - allpassTuningC * allpassTuningBuffer[lastSampleIdx];

        allpassTuningBuffer[bufIdx] = plucked;

        plucked = (1 - R) * plucked + R * lowpassDynamicsBuffer[lastSampleIdx];

        lowpassDynamicsBuffer[bufIdx] = plucked;

        wpL[i] += plucked;
        wpR[i] += plucked;

        if (tailoff && bufIdx == lastTailoffIdx)
        {
            tailoff = false;
            playing = false;
            clearCurrentNote();
        }

        bufIdx = (bufIdx + 1) % bufferSize;
    }
}

void Voice::pitchWheelMoved(int newPitchWheelValue)
{
    calculatePitchInfo(currentNote, newPitchWheelValue);
}

bool Voice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<Sound*> (sound) != nullptr;
}

void Voice::setVoiceParams (float L, float rho, float S, float mu, float t60, float pickStrengthFactor)
{
    mL = L;
    mRho = rho;
    mS = S;
    mMu = mu;
    mT60 = t60;
    mPickStrengthFactor = pickStrengthFactor;
}

void Voice::calculatePitchInfo(int midiNote, int pitchWheelPosition)
{
    currentNote = midiNote;

    // frequency taking into account pitch bend
    float freq = 440 * std::pow(2, (midiNote - 69) / 12.f + (pitchWheelPosition - 8192) / (12.f * 16384 / (2 * mPitchBendSemitones)));
    
    float fs = getSampleRate();
    float omegaNorm = juce::MathConstants<float>::twoPi * freq / fs; // normalized angular frequency

    //eq 22
    float lowpassPhaseDelay = -std::atan(mS * std::sin(omegaNorm) / ((1 - mS) + mS * std::cos(omegaNorm))) / omegaNorm;

    float truePeriod = fs / freq; // unit: samples
    period = (int)(truePeriod - lowpassPhaseDelay - 1e-6);

    float tau = mT60 / std::log(1000);
    rhoTailoff = std::exp(-1 / (freq * tau)) / std::abs(std::cos(omegaNorm / 2));

    float allpassPhaseDelay = truePeriod - period - lowpassPhaseDelay;

    // eq 16
    allpassTuningC = std::sin(0.5 * omegaNorm - 0.5 * omegaNorm * allpassPhaseDelay)
        / std::sin(0.5 * omegaNorm + 0.5 * omegaNorm * allpassPhaseDelay);

    float R_L = std::exp(-juce::MathConstants<float>::pi * mL / fs);
    float G_L = (1 - R_L) / (std::abs(1.f - std::polar<float>(R_L, -middlePitchOmega / fs)));

    R = (1 - G_L * G_L * std::cos(omegaNorm)) / (1 - G_L * G_L);

    float costmp = std::cos(omegaNorm / 2);
    float R_plusminus = 2 * G_L * std::sin(omegaNorm / 2) * std::sqrt(1 - G_L * G_L * costmp * costmp) / (1 - G_L * G_L);

    R += R_plusminus;

    // choose plus or minus depending on which one leads to magnitude is < 1
    if (std::abs(R) >= 1) R -= 2 * R_plusminus;
}

void Voice::createExcitation(float velocity)
{
    // initial burst of noise
    juce::Array<float> noise;

    noise.resize(bufferSize); // wasting memory for indexing convenience
    for (int i = 0; i < period * mPickStrengthFactor; i++)
        noise.set(i, velocity * rand.nextFloat());

    for (int i = 0; i < bufferSize; i++)
    {
        // pick location filter
        int prevIdx = i - (int)(mMu * period);
        float prevVal = prevIdx < 0 ? 0 : noise[prevIdx];
        excitation[i] = noise[i] - prevVal;
    }
}
