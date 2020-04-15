#pragma once
using namespace dsp;

//==============================================================================
class MainContentComponent : public AudioAppComponent,
	public ChangeListener
{
public:
	MainContentComponent()
		: state(Stopped)
	{
		addAndMakeVisible(&openButton);
		openButton.setButtonText("Open...");
		openButton.onClick = [this] { openButtonClicked(); };

		addAndMakeVisible(&playButton);
		playButton.setButtonText("Play");
		playButton.onClick = [this] { playButtonClicked(); };
		playButton.setColour(TextButton::buttonColourId, Colours::green);
		playButton.setEnabled(false);

		addAndMakeVisible(&stopButton);
		stopButton.setButtonText("Stop");
		stopButton.onClick = [this] { stopButtonClicked(); };
		stopButton.setColour(TextButton::buttonColourId, Colours::red);
		stopButton.setEnabled(false);

		setSize(300, 200);

		formatManager.registerBasicFormats();       // [1]
		transportSource.addChangeListener(this);   // [2]

		setAudioChannels(0, 2);
	}

	~MainContentComponent() override
	{
		shutdownAudio();
	}

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
	{
		transportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
	}

	void getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill) override
	{
		if (readerSource.get() == nullptr)
		{
			bufferToFill.clearActiveBufferRegion();
			return;
		}

		auto* device = deviceManager.getCurrentAudioDevice();

		auto activeInputChannels = device->getActiveInputChannels();
		auto activeOutputChannels = device->getActiveOutputChannels();
		auto maxInputChannels = activeInputChannels.countNumberOfSetBits();
		auto maxOutputChannels = activeOutputChannels.countNumberOfSetBits();


		for (auto channel = 0; channel < maxOutputChannels; ++channel) {
			if ((!activeOutputChannels[channel]) || maxInputChannels == 0) {
				bufferToFill.buffer->clear(channel, bufferToFill.startSample, bufferToFill.numSamples);
			}
			else {
				auto actualInputChannel = channel % maxInputChannels;

				if (!activeInputChannels[channel]) {
					bufferToFill.buffer->clear(channel, bufferToFill.startSample, bufferToFill.numSamples);
				}
				else {
					auto* inBuffer = bufferToFill.buffer->getReadPointer(actualInputChannel,
						bufferToFill.startSample);
					auto* outBuffer = bufferToFill.buffer->getWritePointer(channel, bufferToFill.startSample);

					for (auto sample = 0; sample < bufferToFill.numSamples; ++sample) {

						auto currInBuffer = inBuffer[sample];

						outBuffer[sample] = currInBuffer * waveShapeDevided;

						auto filterResult = lowPassFilter.processSample(abs(currInBuffer));

						waveShapeDevided = (float)divider / filterResult;
					}
				}
			}
		}
		transportSource.getNextAudioBlock(bufferToFill);
	}

	void releaseResources() override
	{
		transportSource.releaseResources();
	}

	void resized() override
	{
		openButton.setBounds(10, 10, getWidth() - 20, 20);
		playButton.setBounds(10, 40, getWidth() - 20, 20);
		stopButton.setBounds(10, 70, getWidth() - 20, 20);
	}

	void changeListenerCallback(ChangeBroadcaster* source) override
	{
		if (source == &transportSource)
		{
			if (transportSource.isPlaying())
				changeState(Playing);
			else
				changeState(Stopped);
		}
	}

private:
	enum TransportState
	{
		Stopped,
		Starting,
		Playing,
		Stopping
	};

	void changeState(TransportState newState)
	{
		if (state != newState)
		{
			state = newState;

			switch (state)
			{
			case Stopped:                           // [3]
				stopButton.setEnabled(false);
				playButton.setEnabled(true);
				transportSource.setPosition(0.0);
				break;

			case Starting:                          // [4]
				playButton.setEnabled(false);
				transportSource.start();
				break;

			case Playing:                           // [5]
				stopButton.setEnabled(true);
				break;

			case Stopping:                          // [6]
				transportSource.stop();
				break;
			}
		}
	}

	void openButtonClicked()
	{
		FileChooser chooser("Select a Wave file to play...",
			{},
			"*.wav");                                        // [7]

		if (chooser.browseForFileToOpen())                                    // [8]
		{
			auto file = chooser.getResult();                                  // [9]
			auto* reader = formatManager.createReaderFor(file);              // [10]

			if (reader != nullptr)
			{
				std::unique_ptr<AudioFormatReaderSource> newSource(new AudioFormatReaderSource(reader, true)); // [11]
				transportSource.setSource(newSource.get(), 0, nullptr, reader->sampleRate);                     // [12]
				playButton.setEnabled(true);                                                                    // [13]
				readerSource.reset(newSource.release());                                                        // [14]
			}
		}
	}

	void playButtonClicked()
	{
		changeState(Starting);
	}

	void stopButtonClicked()
	{
		changeState(Stopping);
	}

	//==========================================================================
	TextButton openButton;
	TextButton playButton;
	TextButton stopButton;

	AudioFormatManager formatManager;
	std::unique_ptr<AudioFormatReaderSource> readerSource;
	AudioTransportSource transportSource;
	TransportState state;

	//===============================DSP=========================================

	ProcessorDuplicator<IIR::Filter<float>, IIR::Coefficients<float>> iir;

	int level = 1;
	float divider = 1;
	float amplitudeCutVal = 0;
	float waveShapeDevided = 0;

	double currentSampleRate = 0;
	dsp::IIR::Filter<float>lowPassFilter;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};