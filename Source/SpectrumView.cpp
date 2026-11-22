#include "SpectrumView.h"
#include "MBStudioLookAndFeel.h"

namespace ui = mbstudio::ui;
namespace C = mbstudio::ui::colours;

SpectrumView::SpectrumView (asf::SpectralCleaner& c) : cleaner (c)
{
    in.fill (-120); out.fill (-120); gr.fill (0);
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void SpectrumView::timerCallback()
{
    for (size_t b = 0; b < in.size(); ++b)
    {
        auto follow = [] (float& v, float target, float up, float down) { v += (target - v) * (target > v ? up : down); };
        follow (in[b], cleaner.bandIn[b].load(), .5f, .12f);
        follow (out[b], cleaner.bandOut[b].load(), .5f, .12f);
        follow (gr[b], cleaner.bandGr[b].load(), .35f, .1f);
    }
    repaint();
}

float SpectrumView::xForFreq (float f, juce::Rectangle<float> r) const
{
    return r.getX() + r.getWidth() * std::log10 (f / 20.0f) / 3.0f;
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    ui::paintRecess (g, all);
    const auto r = all.reduced (10.f, 8.f).withTrimmedBottom (14.f);

    // griglia
    const float dbTop = -6.0f, dbBottom = -96.0f;
    auto yForDb = [&] (float db) { return juce::jmap (juce::jlimit (dbBottom, dbTop, db), dbTop, dbBottom, r.getY(), r.getBottom()); };
    g.setColour (C::engrave);
    for (float db = -12; db >= dbBottom; db -= 12) g.drawHorizontalLine ((int) yForDb (db), r.getX(), r.getRight());
    const std::pair<float, const char*> marks[] { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1K" },
                                                  { 2000, "2K" }, { 5000, "5K" }, { 10000, "10K" }, { 20000, "20K" } };
    for (auto& m : marks)
    {
        const float x = xForFreq (m.first, r);
        g.setColour (C::engrave); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        ui::paintPrinted (g, { x - 20, r.getBottom() + 1, 40, 13 }, m.second, 11.f, juce::Justification::centred, C::printDim);
    }

    auto curve = [&] (const std::array<float, asf::SpectralCleaner::numBands>& v)
    {
        juce::Path p;
        const int n = (int) v.size();
        for (int b = 0; b < n; ++b)
        {
            const float f = 20.0f * std::pow (1000.0f, ((float) b + .5f) / (float) n);
            const float x = xForFreq (f, r), y = yForDb (v[(size_t) b]);
            if (b == 0) p.startNewSubPath (r.getX(), y);
            p.lineTo (x, y);
        }
        p.lineTo (r.getRight(), yForDb (v.back()));
        return p;
    };

    // ingresso: area grigia
    auto pin = curve (in);
    auto fill = pin;
    fill.lineTo (r.getRight(), r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
    g.setColour (C::printDim.withAlpha (.13f)); g.fillPath (fill);
    g.setColour (C::printDim.withAlpha (.55f)); g.strokePath (pin, juce::PathStrokeType (1.f));

    // uscita: linea bianca
    g.setColour (C::print.withAlpha (.9f)); g.strokePath (curve (out), juce::PathStrokeType (1.4f));

    // riduzione: ambra appesa in alto (0..-12 dB sul 35% superiore)
    {
        juce::Path p;
        const int n = (int) gr.size();
        const float h = r.getHeight() * .35f;
        p.startNewSubPath (r.getX(), r.getY());
        for (int b = 0; b < n; ++b)
        {
            const float f = 20.0f * std::pow (1000.0f, ((float) b + .5f) / (float) n);
            const float red = juce::jlimit (0.0f, 12.0f, -gr[(size_t) b]);
            p.lineTo (xForFreq (f, r), r.getY() + h * red / 12.0f);
        }
        p.lineTo (r.getRight(), r.getY()); p.closeSubPath();
        g.setColour (C::ledAmber.withAlpha (.22f)); g.fillPath (p);
        g.setColour (C::ledAmber.withAlpha (.85f)); g.strokePath (p, juce::PathStrokeType (1.f));
    }

    // legenda
    auto leg = juce::Rectangle<float> (r.getX() + 6, r.getBottom() - 18, 60, 14);
    ui::paintPrinted (g, leg, "IN", 11.f, juce::Justification::centredLeft, C::printDim); leg.translate (36, 0);
    ui::paintPrinted (g, leg, "OUT", 11.f, juce::Justification::centredLeft, C::print); leg.translate (40, 0);
    ui::paintPrinted (g, leg, "CUT", 11.f, juce::Justification::centredLeft, C::ledAmber);
}
