#pragma once
using namespace dsp;

class Visualizer :public AudioVisualiserComponent {
public:
	Visualizer() : AudioVisualiserComponent(2) {
		setBufferSize(1024);
		setSamplesPerBlock(512);
		setColours(Colours::black, Colours::indianred);
	}
};

//==============================================================================
class MainContentComponent : public AudioAppComponent,
	public ChangeListener,
	public Timer
{
public:
	MainContentComponent()
		: state(Stopped)
	{
		addAndMakeVisible(&openButton);
		openButton.setButtonText("Open...");
		openButton.onClick = [this] { openButtonClicked(); };

		addAndMakeVisible(&playButton);
		playButton.setButtonText("Play/Start Writing");
		playButton.onClick = [this] { playButtonClicked(); };
		playButton.setColour(TextButton::buttonColourId, Colours::green);
		playButton.setEnabled(false);

		addAndMakeVisible(&stopButton);
		stopButton.setButtonText("Stop/End Writing");
		stopButton.onClick = [this] { stopButtonClicked(); };
		stopButton.setColour(TextButton::buttonColourId, Colours::red);
		stopButton.setEnabled(false);

		//addAndMakeVisible(&loopingToggle);
		loopingToggle.setButtonText("Loop");
		loopingToggle.onClick = [this] { loopButtonChanged(); };

		addAndMakeVisible(&processToggle);
		processToggle.setButtonText("Process");
		processToggle.onClick = [this] {isProcessing = !isProcessing; };

		addAndMakeVisible(&currentPositionLabel);
		currentPositionLabel.setText("Stopped", dontSendNotification);

		addAndMakeVisible(inputLabel);
		inputLabel.setText("Input", dontSendNotification);
		addAndMakeVisible(outputLabel);
		outputLabel.setText("Output", dontSendNotification);

		addAndMakeVisible(inputVisualizer);
		addAndMakeVisible(outputVisualizer);

		setSize(600, 400);

		formatManager.registerBasicFormats();
		transportSource.addChangeListener(this);

		setAudioChannels(2, 2);
		startTimer(20);
	}

	~MainContentComponent() override
	{
		shutdownAudio();
	}

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
	{
		transportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
		lowPassFilter = dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 16);
		inputVisualizer.clear();
		outputVisualizer.clear();
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
		transportSource.getNextAudioBlock(bufferToFill);

		inputVisualizer.pushBuffer(bufferToFill);

		if (isProcessing) {
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

							if (abs(currInBuffer) < amplitudeCutVal) {
								currInBuffer = currInBuffer / 2;
							}
							else {
								auto filterResult = lowPassFilter.processSample(abs(currInBuffer));

								waveShapeDevided = (float)divider / filterResult;
							}

							outBuffer[sample] = currInBuffer * waveShapeDevided;
						}
					}
				}
			}
		}

		outputVisualizer.pushBuffer(bufferToFill);

		//=============================================================================

		if (writer != nullptr && isWriting) {
			writer->writeFromAudioSampleBuffer(AudioSampleBuffer(*bufferToFill.buffer), bufferToFill.startSample, bufferToFill.numSamples);
		}
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
		//loopingToggle.setBounds(10, 100, getWidth() - 20, 20);
		processToggle.setBounds(10, 100, getWidth() - 10, 20);
		currentPositionLabel.setBounds(510, 100, getWidth() - 20, 20);

		inputLabel.setBounds(10, 140, 90, 20);
		outputLabel.setBounds(10, 270, 90, 20);

		inputVisualizer.setBounds(10, 160, getWidth() - 20, 100);
		outputVisualizer.setBounds(10, 290, getWidth() - 20, 100);
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

	void timerCallback() override
	{
		if (transportSource.isPlaying())
		{
			RelativeTime position(transportSource.getCurrentPosition());

			auto minutes = ((int)position.inMinutes()) % 60;
			auto seconds = ((int)position.inSeconds()) % 60;
			auto millis = ((int)position.inMilliseconds()) % 1000;

			auto positionString = String::formatted("%02d:%02d:%03d", minutes, seconds, millis);

			currentPositionLabel.setText(positionString, dontSendNotification);
		}
		else
		{
			currentPositionLabel.setText("Stopped", dontSendNotification);
		}
	}

	void updateLoopState(bool shouldLoop)
	{
		if (readerSource.get() != nullptr)
			readerSource->setLooping(shouldLoop);
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
			case Stopped:
				stopButton.setEnabled(false);
				playButton.setEnabled(true);
				transportSource.setPosition(0.0);
				stopWriting();
				break;

			case Starting:
				playButton.setEnabled(false);
				transportSource.start();
				startWriting();
				break;

			case Playing:
				stopButton.setEnabled(true);
				break;

			case Stopping:
				transportSource.stop();
				stopWriting();
				break;
			}
		}
	}


	void openButtonClicked()
	{
		FileChooser chooser("Select a Wave file to play...",
			{},
			"*.wav");
		
		if (chooser.browseForFileToOpen())
		{
			inFile = chooser.getResult();
			reader = formatManager.createReaderFor(inFile);

			if (reader != nullptr)
			{
				std::unique_ptr<AudioFormatReaderSource> newSource(new AudioFormatReaderSource(reader, true));
				transportSource.setSource(newSource.get(), 0, nullptr, reader->sampleRate);
				playButton.setEnabled(true);
				readerSource.reset(newSource.release());

			}
		}
	}

	void startWriting() {

		resFile = File(File::getSpecialLocation(File::userDocumentsDirectory).getNonexistentChildFile(inFile.getFileNameWithoutExtension() + "Processed",".wav"));
		resFile.deleteFile();
		auto fos = new FileOutputStream(resFile);

		writer.reset(format.createWriterFor(fos, reader->sampleRate, reader->numChannels, reader->bitsPerSample, reader->metadataValues, 0));
		isWriting = true;
	}

	void stopWriting() {
		isWriting = false;
		writer = nullptr;
	}

	void playButtonClicked()
	{
		updateLoopState(loopingToggle.getToggleState());
		changeState(Starting);
	}

	void stopButtonClicked()
	{
		changeState(Stopping);
	}

	void loopButtonChanged()
	{
		updateLoopState(loopingToggle.getToggleState());
	}

	//=================================UI=======================================
	TextButton openButton;
	TextButton playButton;
	TextButton stopButton;

	ToggleButton loopingToggle;
	ToggleButton processToggle;
	Label currentPositionLabel;

	Label inputLabel;
	Label outputLabel;

	Visualizer inputVisualizer;
	Visualizer outputVisualizer;

	//==========================================================================

	AudioFormatManager formatManager;
	AudioFormatReader* reader;
	std::unique_ptr<AudioFormatReaderSource> readerSource;
	AudioTransportSource transportSource;
	TransportState state;

	WavAudioFormat format;
	std::unique_ptr<AudioFormatWriter> writer;
	File inFile;
	File resFile;

	//===============================DSP=========================================

	ProcessorDuplicator<IIR::Filter<float>, IIR::Coefficients<float>> iir;

	bool isWriting=false;
	bool isProcessing = false;
	float divider = 0.1;
	float amplitudeCutVal = 0.05;
	float waveShapeDevided = 0;

	dsp::IIR::Filter<float>lowPassFilter;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};
