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

	// create a bounding box that can centre our text around our slider
	auto center = sliderBounds.toFloat().getCentre();
	auto radius = sliderBounds.getWidth() * 0.5f;

	g.setColour(Colour(0u, 172u, 1u));
	g.setFont(getTextHeight());

	auto numChoices = labels.size();
	for (int i = 0; i < numChoices; ++i)
	{
		auto pos = labels[i].pos;
		jassert(0.f <= pos);
		jassert(pos <= 1.f);

		auto angle = jmap(pos, 0.f, 1.f, startAngle, endAngle);

		// calculate the position of our text
		auto c = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 1, angle);

		Rectangle<float> r;
		auto str = labels[i].label;
		r.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
		r.setCentre(c);
		r.setY(r.getY() + getTextHeight());

		g.drawFittedText(str, r.toNearestInt(), juce::Justification::centred, 1);
	}
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
ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p) : 
	audioProcessor(p),
	//leftChannelFifo(&audioProcessor.leftChannelFifo)
	leftPathProducer(audioProcessor.leftChannelFifo),
	rightPathProducer(audioProcessor.rightChannelFifo)
{
	const auto& params = audioProcessor.getParameters();
	for (auto param : params)
	{
		param->addListener(this);
	}

	// if our sample rate is 48000 and we have 2048 bins each bin accounts for 23 hertz.
	// Because of this we will get very high resolution above 1k hz but very low
	// resolution below 1k hz



	updateChain();

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

void PathProducer::process(juce::Rectangle<float> fftBounds, double sampleRate)
{
	juce::AudioBuffer<float> tempIncomingBuffer;

	while (leftChannelFifo->getNumCompleteBuffersAvailable() > 0)
	{
		if (leftChannelFifo->getAudioBuffer(tempIncomingBuffer))
		{
			auto size = tempIncomingBuffer.getNumSamples();
			juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, 0),
				monoBuffer.getReadPointer(0, size),
				monoBuffer.getNumSamples() - size);

			juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, monoBuffer.getNumSamples() - size),
				tempIncomingBuffer.getReadPointer(0, 0),
				size);

			leftChannelFFTDataGenerator.produceFFTDataForRendering(monoBuffer, -48.f);
		}
	}

	// if there are FFT data buffers, pull one and generate a path
	const auto fftSize = leftChannelFFTDataGenerator.getFFTSize();

	// bin width is the sample rate divided by the bin size
	const auto binWidth = sampleRate / (double)fftSize;

	while (leftChannelFFTDataGenerator.getNumAvailableFFTDataBlocks() > 0)
	{
		std::vector<float> fftData;
		if (leftChannelFFTDataGenerator.getFFTData(fftData))
		{
			pathProducer.generatePath(fftData, fftBounds, fftSize, binWidth, -48.f);
		}
	}

	// while there are paths that can be pulled, pull as many as we can, and display most recent
	while (pathProducer.getNumPathsAvailable())
	{
		pathProducer.getPath(leftChannelFFTPath);
	}
}

void ResponseCurveComponent::timerCallback()
{
	auto fftBounds = getAnalysisArea().toFloat();
	auto sampleRate = audioProcessor.getSampleRate();

	leftPathProducer.process(fftBounds, sampleRate);
	rightPathProducer.process(fftBounds, sampleRate);

	if (parametersChanged.compareAndSetBool(false, true))
	{
		// update the monochain
		updateChain();
		// signal a repaint
		//repaint();
	}

	repaint();
}

void ResponseCurveComponent::updateChain()
{
	auto chainSettings = getChainSettings(audioProcessor.apvts);
	auto peakCoefficients = makePeakFilter(chainSettings, audioProcessor.getSampleRate());
	updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);

	auto loCutCoefficients = makeLoCutFilter(chainSettings, audioProcessor.getSampleRate());
	auto hiCutCoefficients = makeHiCutFilter(chainSettings, audioProcessor.getSampleRate());

	updateCutFilter(monoChain.get<ChainPositions::LoCut>(), loCutCoefficients, chainSettings.loCutSlope);
	updateCutFilter(monoChain.get<ChainPositions::HiCut>(), hiCutCoefficients, chainSettings.hiCutSlope);
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
	using namespace juce; // save us from typing juce:: everywhere
	// (Our component is opaque, so we must completely fill the background with a solid colour)
	g.fillAll(Colours::black);

	g.drawImage(background, getLocalBounds().toFloat());

	auto responseArea = getAnalysisArea();

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

	auto leftChannelFFTPath = leftPathProducer.getPath();
	leftChannelFFTPath.applyTransform(AffineTransform().translation(responseArea.getX(), responseArea.getY()));

	g.setColour(Colours::lightblue);
	g.strokePath(leftChannelFFTPath, PathStrokeType(1));

	auto rightChannelFFTPath = rightPathProducer.getPath();
	rightChannelFFTPath.applyTransform(AffineTransform().translation(responseArea.getX(), responseArea.getY()));

	g.setColour(Colours::lightyellow);
	g.strokePath(rightChannelFFTPath, PathStrokeType(1));

	g.setColour(Colours::orange);
	g.drawRoundedRectangle(getRenderArea().toFloat(), 4.f, 1.f);

	g.setColour(Colours::white);
	g.strokePath(responseCurve, PathStrokeType(2.f));
};

void ResponseCurveComponent::resized()
{
	using namespace juce;
	background = Image(Image::PixelFormat::RGB, getWidth(), getHeight(), true);

	Graphics g(background);

	// draw frequency as vcrtical lines
	Array<float> freqs
	{
		20, /*30, 40,*/ 50, 100, 
		200, /*300, 400,*/ 500, 1000,
		2000, /*3000, 4000,*/ 5000, 10000,
		20000
	};

	auto renderArea = getAnalysisArea();
	auto left = renderArea.getX();
	auto right = renderArea.getRight();
	auto top = renderArea.getY();
	auto bottom = renderArea.getBottom();
	auto width = renderArea.getWidth();

	Array<float> xs;
	for (auto f : freqs)
	{
		auto normX = mapFromLog10(f, 20.f, 20000.f);
		xs.add(left + width * normX);
	}

	// set the frequcny lines to white iterate through the frequencies and draw them
	g.setColour(Colours::dimgrey);
	for (auto x : xs)
	{
		g.drawVerticalLine(x, top, bottom);
	}

	Array<float> gain
	{
		-24, -12, 0, 12, 24
	};

	for (auto gDb : gain)
	{
		auto y = jmap(gDb, -24.f, 24.f, float(bottom), float(top));

		// TODO standarize this colour at the top of the header file
		g.setColour(gDb == 0.f ? Colour(0u, 172u, 1u) : Colours::darkgrey);
		g.drawHorizontalLine(y, left, right);
	}

	g.setColour(Colours::lightgrey);
	const int fontHeight = 10;
	g.setFont(fontHeight);

	// loop through freq and x and draw grid at those positions
	for (int i = 0; i < freqs.size(); ++i)
	{
		auto f = freqs[i];
		auto x = xs[i];
		// TODO we use this functionality twice, can we extract to a commmon function?
		bool addK = false;
		String str;
		if (f > 999.f)
		{
			f /= 1000.f;
			addK = true;
		}
		str << f;
		if (addK)
			str << "k";
		str << "Hz";

		auto textWidth = g.getCurrentFont().getStringWidth(str);

		Rectangle<int> r;
		r.setSize(textWidth, fontHeight);
		r.setCentre(x, 0);
		r.setY(1);

		g.drawFittedText(str, r, juce::Justification::centred, 1);
	}

	for (auto gDb : gain)
	{
		auto y = jmap(gDb, -24.f, 24.f, float(bottom), float(top));
		String str;
		if (gDb > 0)
			str << "+";
		str << gDb;

		auto textWidth = g.getCurrentFont().getStringWidth(str);
		Rectangle<int> r;
		r.setSize(textWidth, fontHeight);
		r.setX(getWidth() - textWidth);
		r.setCentre(r.getCentreX(), y);

		g.setColour(gDb == 0.f ? Colour(0u, 172u, 1u) : Colours::lightgrey);
		g.drawFittedText(str, r, juce::Justification::centred, 1);

		str.clear();
		str << (gDb - 24.f);

		r.setX(1);
		textWidth = g.getCurrentFont().getStringWidth(str);
		r.setSize(textWidth, fontHeight);
		g.setColour(Colours::lightgrey);
		g.drawFittedText(str, r, juce::Justification::centred, 1);
	}
}

juce::Rectangle<int> ResponseCurveComponent::getRenderArea()
{
	auto bounds = getLocalBounds();

	bounds.removeFromTop(12);
	bounds.removeFromBottom(2);
	bounds.removeFromLeft(20);
	bounds.removeFromRight(20);

	return bounds;
}

juce::Rectangle<int> ResponseCurveComponent::getAnalysisArea()
{
	auto bounds = getRenderArea();
	bounds.removeFromTop(4);
	bounds.removeFromBottom(4);
	return bounds;
}
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

	peakFreqSlider.labels.add({ 0.f, "20Hz" });
	peakFreqSlider.labels.add({ 1.f, "20kHz" });

	peakGainSlider.labels.add({ 0.f, "-24dB" });
	peakGainSlider.labels.add({ 1.f, "+24dB" });

	peakQualitySlider.labels.add({ 0.f, "0.1" });
	peakQualitySlider.labels.add({ 1.f, "10.0" });

	loCutFreqSlider.labels.add({ 0.f, "20Hz" });
	loCutFreqSlider.labels.add({ 1.f, "20kHz" });

	hiCutFreqSlider.labels.add({ 0.f, "20Hz" });
	hiCutFreqSlider.labels.add({ 1.f, "20kHz" });

	loCutSlopeSlider.labels.add({ 0.f, "12" });
	loCutSlopeSlider.labels.add({ 1.f, "48" });

	hiCutSlopeSlider.labels.add({ 0.f, "12" });
	hiCutSlopeSlider.labels.add({ 1.f, "48" });

	for (auto* comp : getComps())
	{
		addAndMakeVisible(comp);
	}

	setSize(600, 480);
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
	float hRatio = 25.f / 100.f; // JUCE_LIVE_CONSTANT(33) / 100.f;
	auto resposneArea = bounds.removeFromTop(bounds.getHeight() * hRatio);

	responseCurveComponent.setBounds(resposneArea);

	bounds.removeFromTop(5);

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
