#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Dsp/SpectralCleaner.h"

// Display incassato: spettro in ingresso (grigio), in uscita (bianco) e riduzione per banda (ambra, in alto)
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (asf::SpectralCleaner& c);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    float xForFreq (float f, juce::Rectangle<float> r) const;

    asf::SpectralCleaner& cleaner;
    std::array<float, asf::SpectralCleaner::numBands> in {}, out {}, gr {};
};
