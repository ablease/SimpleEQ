/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void LookAndFeel::drawRotarySlider(
	juce::Graphics& g,
	int x,
	int y,
	int width,
	int height,
	float sliderPosProportional,
	float rotaryStartAngle,
	float rotaryEndAngle,
	juce::Slider& slider)
{
	using namespace juce;
	auto bounds = Rectangle<float>(x, y, width, height);

	g.setColour(Colour(192u, 108u, 167u));
	g.fillEllipse(bounds); // fill an ellipse

	g.setColour(Colour(255u, 154u, 255u));
	g.drawEllipse(bounds, 1.f); // draw an ellipse border

	if (auto* rswl = dynamic_cast<RotarySliderWithLabels*>(&slider))
	{
		auto center = bounds.getCentre();

		// to rotate something we must define it in a path
		Path p;

		// this will be the Dial highlight or marker
		Rectangle<float> r;
		r.setLeft(center.getX() - 2);
		r.setRight(center.getX() + 2);
		r.setTop(bounds.getY());
		r.setBottom(center.getY() - rswl->getTextHeight() * 1.5);

		p.addRoundedRectangle(r, 2.f);
		jassert(rotaryStartAngle < rotaryEndAngle);

		auto sliderAngleInRadians = jmap(sliderPosProportional, 0.f, 1.f, rotaryStartAngle, rotaryEndAngle);

		// rotate the dial marker around the centre of our component
		p.applyTransform(AffineTransform().rotated(sliderAngleInRadians, center.getX(), center.getY()));

		g.fillPath(p);

		g.setFont(rswl->getTextHeight());

		auto text = rswl->getDisplayString();
		auto stringWidth = g.getCurrentFont().getStringWidth(text);

		r.setSize(stringWidth + 4, rswl->getTextHeight() + 2);
		r.setCentre(bounds.getCentre());

		g.setColour(Colours::black);
		g.fillRect(r);

		g.setColour(Colours::white);
		g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
	}
}

// we want to the values of 0 - 1 to be mapped from 7 oclock to 5 oclock in a clockwise fashion
// 12 oclock is 0 degress in radians
void RotarySliderWithLabels::paint(juce::Graphics& g)
{
	using namespace juce;

	auto startAngle = degreesToRadians(180.f + 45.f);
	auto endAngle = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi; // this makes it easier to use jmap

	auto range = getRange();

	auto sliderBounds = getSliderBounds();

	// draw bounding boxes for our components
	//g.setColour(Colours::red);
	//g.drawRect(getLocalBounds());
	//g.setColour(Colours::yellow);
	//g.drawRect(sliderBounds);

	getLookAndFeel().drawRotarySlider(
		g,
		sliderBounds.getX(),
		sliderBounds.getY(),
		sliderBounds.getWidth(),
		sliderBounds.getHeight(),
		jmap(getValue(), range.getStart(), range.getEnd(), 0.0, 1.0), // map sliders value to noramlised value 0.0-1.0
		startAngle,
		endAngle,
		*this);
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const
{
	auto bounds = getLocalBounds();

	auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());

	size -= getTextHeight() * 2;

	juce::Rectangle<int> r;
	r.setSize(size, size);
	r.setCentre(bounds.getCentreX(), 0);
	r.setY(2);

	return r;
}

juce::String RotarySliderWithLabels::getDisplayString() const
{
	if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
		return choiceParam->getCurrentChoiceName();

	juce::String str;
	bool addK = false;

	// if our value is greater than 999 we divide by 1000 and append a k to our display string
	if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
	{
		float val = getValue();
		if (val > 999.f)
		{
			val /= 1000.f;
			addK = true;
		}
		str = juce::String(val, (addK ? 2 : 0)); // 2 decimal places if we have a k
	}
	else
	{
		jassertfalse; // this should never happen
	}

	// if we have a suffix we append a space and the suffix to our string
	if (suffix.isNotEmpty())
	{
		str << " ";
		if (addK)
			str << "k";
		str << suffix;
	}

	return str;
}

//==============================================================================
ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p) : audioProcessor(p)
{
	const auto& params = audioProcessor.getParameters();
	for (auto param : params)
	{
		param->addListener(this);
	}

	startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
	const auto& params = audioProcessor.getParameters();
	for (auto param : params)
	{
		param->removeListener(this);
	}
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
	parametersChanged.set(true);
}

void ResponseCurveComponent::timerCallback()
{
	if (parametersChanged.compareAndSetBool(false, true))
	{
		// update the monochain
		auto chainSettings = getChainSettings(audioProcessor.apvts);
		auto peakCoefficients = makePeakFilter(chainSettings, audioProcessor.getSampleRate());
		updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);

		auto loCutCoefficients = makeLoCutFilter(chainSettings, audioProcessor.getSampleRate());
		auto hiCutCoefficients = makeHiCutFilter(chainSettings, audioProcessor.getSampleRate());

		updateCutFilter(monoChain.get<ChainPositions::LoCut>(), loCutCoefficients, chainSettings.loCutSlope);
		updateCutFilter(monoChain.get<ChainPositions::HiCut>(), hiCutCoefficients, chainSettings.hiCutSlope);
		// signal a repaint
		repaint();
	}
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
	using namespace juce; // save us from typing juce:: everywhere
	// (Our component is opaque, so we must completely fill the background with a solid colour)
	g.fillAll(Colours::black);

	auto responseArea = getLocalBounds();

	auto w = responseArea.getWidth();

	auto& loCut = monoChain.get<ChainPositions::LoCut>();
	auto& peak = monoChain.get<ChainPositions::Peak>();
	auto& hiCut = monoChain.get<ChainPositions::HiCut>();

	auto sampleRate = audioProcessor.getSampleRate();

	std::vector<double> mags;

	mags.resize(w);

	for (int i = 0; i < w; i++)
	{
		double mag = 1.f;
		auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);

		if (!monoChain.isBypassed<ChainPositions::Peak>())
			mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);

		if (!loCut.isBypassed<0>())
			mag *= loCut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!loCut.isBypassed<1>())
			mag *= loCut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!loCut.isBypassed<2>())
			mag *= loCut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!loCut.isBypassed<3>())
			mag *= loCut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

		if (!hiCut.isBypassed<0>())
			mag *= hiCut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!hiCut.isBypassed<1>())
			mag *= hiCut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!hiCut.isBypassed<2>())
			mag *= hiCut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!hiCut.isBypassed<3>())
			mag *= hiCut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

		mags[i] = Decibels::gainToDecibels(mag);
	}

	Path responseCurve;

	const double outputMin = responseArea.getBottom();
	const double outputMax = responseArea.getY();
	auto map = [outputMin, outputMax](double input)
		{
			return jmap(input, -24.0, 24.0, outputMin, outputMax);
		};

	responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));

	for (size_t i = 1; i < mags.size(); ++i)
	{
		responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
	}

	g.setColour(Colours::orange);
	g.drawRoundedRectangle(responseArea.toFloat(), 4.f, 1.f);

	g.setColour(Colours::white);
	g.strokePath(responseCurve, PathStrokeType(2.f));
};

//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor(SimpleEQAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p),

	peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"),
	peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "dB"),
	peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
	loCutFreqSlider(*audioProcessor.apvts.getParameter("LoCut Freq"), "Hz"),
	hiCutFreqSlider(*audioProcessor.apvts.getParameter("HiCut Freq"), "Hz"),
	loCutSlopeSlider(*audioProcessor.apvts.getParameter("LoCut Slope"), "dB/Oct"),
	hiCutSlopeSlider(*audioProcessor.apvts.getParameter("HiCut Slope"), "dB/Oct"),

	responseCurveComponent(audioProcessor),
	peakFreqSliderAttachment(audioProcessor.apvts, "Peak Freq", peakFreqSlider),
	peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
	peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
	loCutFreqSliderAttachment(audioProcessor.apvts, "LoCut Freq", loCutFreqSlider),
	hiCutFreqSliderAttachment(audioProcessor.apvts, "HiCut Freq", hiCutFreqSlider),
	loCutSlopeSliderAttachment(audioProcessor.apvts, "LoCut Slope", loCutSlopeSlider),
	hiCutSlopeSliderAttachment(audioProcessor.apvts, "HiCut Slope", hiCutSlopeSlider)
{
	// Make sure that before the constructor has finished, you've set the
	// editor's size to whatever you need it to be.

	for (auto* comp : getComps())
	{
		addAndMakeVisible(comp);
	}

	setSize(600, 400);
}

SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{

}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint(juce::Graphics& g)
{
	using namespace juce; // save us from typing juce:: everywhere
	// (Our component is opaque, so we must completely fill the background with a solid colour)
	g.fillAll(Colours::black);
}

void SimpleEQAudioProcessorEditor::resized()
{
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..

	auto bounds = getLocalBounds();
	auto resposneArea = bounds.removeFromTop(bounds.getHeight() * 0.33);

	responseCurveComponent.setBounds(resposneArea);

	auto loCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
	auto hiCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);; // half of whats left (another 1/3)

	loCutFreqSlider.setBounds(loCutArea.removeFromTop(loCutArea.getHeight() * 0.5));
	loCutSlopeSlider.setBounds(loCutArea);

	hiCutFreqSlider.setBounds(hiCutArea.removeFromTop(hiCutArea.getHeight() * 0.5));
	hiCutSlopeSlider.setBounds(hiCutArea);

	peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
	peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
	peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> SimpleEQAudioProcessorEditor::getComps()
{
	return
	{
		&peakFreqSlider,
		&peakGainSlider,
		&peakQualitySlider,
		&loCutFreqSlider,
		&hiCutFreqSlider,
		&loCutSlopeSlider,
		&hiCutSlopeSlider,
		&responseCurveComponent
	};
}
