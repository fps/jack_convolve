/*
    Copyright (C) 2004 Florian Schmidt
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software 
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "convolve.h"
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <jack/jack.h>
#include <sstream>
#include <sndfile.h>
#include <samplerate.h>
#include <vector>
#include <signal.h>
#include <cmath>

// can be overridden by cmdline
std::string jack_name = "jack_convolve";

jack_client_t *client;
convolution_t conv;

jack_port_t **iports;
jack_port_t **oports;

float **in_data;
int in_data_size;
float **out_data;
int out_data_size;

std::vector<std::string> response_file_names;

bool fake_mode = 0;

float gain = 1.0;
int min_bin = -1;
int max_bin = -1;

int process(jack_nframes_t frames, void* arg) {

	for (int i = 0; i < in_data_size; ++i) 
		in_data[i] = (float*)jack_port_get_buffer(iports[i], frames);

	for (int i = 0; i < out_data_size; ++i) 
		out_data[i] = (float*)jack_port_get_buffer(oports[i], frames);

	convolution_process(&conv, in_data, out_data, gain, min_bin, max_bin);

	return 0;
}

void extract_response_file_names_from_cl (int argc, char *argv[]) {

	// the first one is the name of the executable.
	// the following are possible filenames
	for (int i = 1; i < argc; ++i) {

		if(!(std::string(argv[i]).substr(0,2) == "--")) {

			// found a param which is not an option,
			// so it's a filename
			response_file_names.push_back(argv[i]);
		}
	}
}

void print_usage() {

	std::cout << "usage: jack_convolve <options> <responsefiles>" << std::endl;
	std::cout << "where <options> are: " << std::endl;
	std::cout << "--name=name       set the name used when registering to jack" << std::endl;
	std::cout << "--gain=factor     apply gain factor (default 1.0)" << std::endl;
	std::cout << "--min_bin=bin_no  which bin to start from (default 0)" << std::endl;
	std::cout << "--max_bin=bin_no  which bin to end with (default buffersize + 1" << std::endl;
	std::cout << "--help            show this help" << std::endl;
	std::cout << "where <responsefiles> are soundfiles with an equal channels count" << std::endl;
}

void extract_options_from_cl (int argc, char *argv[]) {

	for (int i = 1; i < argc; ++i) {

		if(std::string(argv[i]).substr(0,std::string("--help").length()) == "--help") {

			print_usage();
			exit (0);
		}

		if(std::string(argv[i]).substr(0,std::string("--fake-mode").length()) == "--fake-mode") {

			fake_mode = true;
		}

		if(std::string(argv[i]).substr(0,std::string("--version").length()) == "--version") {

			std::cout << "version 0.0.9" << std::endl;
			exit (0);
		}

		if(std::string(argv[i]).substr(0,std::string("--gain=").length()) == "--gain=") {

			std::stringstream stream;
			stream << (std::string(argv[i]).substr(std::string("--gain=").length(),
			                                      std::string(argv[i]).length() 
			                                       - std::string("--gain=").length()));

			stream >> gain;
			// std::cout << "gain = " << gain << std::endl;
		}

		if(std::string(argv[i]).substr(0,std::string("--min_bin=").length()) == "--min_bin=") {

			std::stringstream stream;
			stream << (std::string(argv[i]).substr(std::string("--min_bin=").length(),
			                                      std::string(argv[i]).length() 
			                                       - std::string("--min_bin=").length()));

			stream >> min_bin;
			// std::cout << "gain = " << gain << std::endl;
		}

		if(std::string(argv[i]).substr(0,std::string("--max_bin=").length()) == "--max_bin=") {

			std::stringstream stream;
			stream << (std::string(argv[i]).substr(std::string("--max_bin=").length(),
			                                      std::string(argv[i]).length() 
			                                       - std::string("--max_bin=").length()));

			stream >> max_bin;
			// std::cout << "gain = " << gain << std::endl;
		}

		if(std::string(argv[i]).substr(0,std::string("--name=").length()) == "--name=") {

			// and extract what's after it
			std::stringstream stream;
			stream << (std::string(argv[i]).substr(std::string("--name=").length(),
			                                      std::string(argv[i]).length() 
			                                       - std::string("--name=").length()));
			
			stream >> jack_name;
			std::cout << "using jack name: \"" << jack_name << "\"" << std::endl;
		}
	}
}

bool quit = false;
void signalled(int sig) {
	std::cout << "exiting.." << std::endl;
	quit = true;
}

int main(int argc, char *argv[]) {
	std::cout << "jack_convolve (C) 2004 Florian Schmidt - protected by GPL" << std::endl;

	// hook up signal handler for ctrl-c
	signal(SIGINT, signalled);
	
	libconvolve_init();

	extract_options_from_cl(argc, argv);

	extract_response_file_names_from_cl(argc, argv);
	if (response_file_names.size() == 0) {
		print_usage();
		exit (0);
	}

	// register jack client
	client = jack_client_new(jack_name.c_str());
	if (!client) {
		std::cout << "Error: couldn't become jack client - server running? name already taken?" << std::endl;
		exit(0);
	}

	// find out the samplerate of jack
	int jack_sample_rate = jack_get_sample_rate(client);

	// allocate array for pointers to the responses
	response_t **responses = (response_t**)malloc(sizeof(response_t*) * response_file_names.size());

	// channels in the response files
	int response_channels = 0;

	for (unsigned int index = 0; index < response_file_names.size(); ++index) {

		// allocate space for response structs
		responses[index] = (convolution_response*)malloc(sizeof(response_t));

		std::cout << "reading response file: " <<  response_file_names[index] << std::endl;

		// open each response file
		struct SF_INFO sf_info;
		SNDFILE *response_file = sf_open (response_file_names[index].c_str(), SFM_READ, &sf_info);	

		if (response_file == 0) {

			std::cout << "couldn't open: " << response_file_names[index] << ". exiting." << std::endl;
			exit (0);
		}

		// check for channel mismatch
		if (index == 0) {

			// store the channel count of the first response file
			response_channels = sf_info.channels;

			// std::cout << "channels: " << response_channels << std::endl; 
		}
		else {
			// compare channel count of current file with the first
			if (response_channels != sf_info.channels) {

				std::cout << "channel count mismatch in response files. exiting." << std::endl;
				exit(0);
			}
		}			

		// a temporary storage for the sample data in the response file
		std::vector<std::vector<float> > channel_data;

		channel_data.resize(sf_info.channels);

		for (int channel = 0; channel < sf_info.channels; ++channel) {
			channel_data[channel].resize(sf_info.frames);
		}

		// std::cout << __LINE__ << std::endl;

		// read data from file into temporary channel_data
		float *tmp = new float[sf_info.channels];
		for (int frame = 0; frame < sf_info.frames; ++frame) {

			if (sf_readf_float(response_file, tmp, 1) != 1) {
				std::cout << "problem reading file. exiting.." << std::endl;
				exit(0);
			} 		

			for (int channel = 0; channel < sf_info.channels; ++channel) {
				channel_data[channel][frame] = tmp[channel];
			}
		}
		delete[] tmp;

		// std::cout << __LINE__ << std::endl;

		// resize channel_data array in response file structure
		responses[index]->channel_data = (float**)malloc(sizeof(float*)*sf_info.channels);

		unsigned int frames = sf_info.frames;

		// do we need to resample or simply copy?
		if (sf_info.samplerate != jack_sample_rate) {

			std::cout << "samplerate mismatch.. resampling: " << response_file_names[index] << std::endl;

			frames = (unsigned int) ceil(((float)sf_info.frames * ((float)jack_sample_rate / (float)sf_info.samplerate)));

	 		// allocate buffers for channel data in response struct
			for (int channel = 0; channel < sf_info.channels; ++channel) {	

				responses[index]->channel_data[channel] = (float*)malloc(sizeof(float) * frames);

				// and zero them (for the case that the samplerate conversion
				// uses less frames than available
				for (unsigned int frame = 0; frame < frames; ++frame) {
					responses[index]->channel_data[channel][frame] = 0;
				}
			}

			// for each channel
			for (int channel = 0; channel < sf_info.channels; ++channel) { 
	
				SRC_DATA src_data;				
				
				src_data.data_in = &channel_data[channel][0];
				src_data.data_out = responses[index]->channel_data[channel];

				src_data.input_frames = sf_info.frames;
				src_data.output_frames = frames;

				src_data.src_ratio = (float)frames / (float)sf_info.frames;				

				src_simple(&src_data, SRC_SINC_BEST_QUALITY, 1);
			}	
		} 
		// no resampling, so it's a simple copy
		else {

			std::cout << frames << std::endl;

			// allocate buffers for channel data and copy data
			for (int channel = 0; channel < sf_info.channels; ++channel) {	

				responses[index]->channel_data[channel] = (float*)malloc(sizeof(float) * frames);

				for (unsigned int frame = 0; frame < frames; ++frame) {
					responses[index]->channel_data[channel][frame] = channel_data[channel][frame];	
				}
			}
		}

		std::cout << "length: " << frames << " frames / " << (float)frames/(float)jack_sample_rate << " seconds" << std::endl;

		responses[index]->length = frames;
		responses[index]->channels = sf_info.channels;

		sf_close(response_file);
	} // for each response

	std::cout << "registering ports:";

	// allocate iports array
	iports = (jack_port_t**)malloc(sizeof(jack_port_t*) * response_file_names.size());

	// register ports
	for (unsigned int i = 0; i < response_file_names.size(); ++i) {

		std::stringstream stream_in;
		stream_in << "in" << i;
		std::cout << " " << stream_in.str();
		iports[i] = jack_port_register(client, stream_in.str().c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	}

	// allocate oports array
	oports = (jack_port_t**)malloc(sizeof(jack_port_t*) * response_channels);

	// register ports
	for (int i = 0; i < response_channels; ++i) {

		std::stringstream stream_out;
		stream_out << "out" << i;
		std::cout << " " << stream_out.str();
		oports[i] = jack_port_register(client, stream_out.str().c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	}
	
	std::cout << std::endl;

	in_data = (float**)malloc(sizeof(float*) * response_file_names.size());
	in_data_size = response_file_names.size();

	out_data = (float**)malloc(sizeof(float*) * response_channels);
	out_data_size = response_channels;

	std::cout << "initializing convolution (# of responsefiles: " << response_file_names.size() << ". channels: " << response_channels << ". buffer size: " << jack_get_buffer_size(client) << ")...";

	convolution_init(&conv, response_file_names.size(), response_channels, responses, jack_get_buffer_size(client));
	
	std::cout << "done." << std::endl;

	if (fake_mode) {

		// we only call the convolution code until user wants us to exit..
		// for this we need some buffers

		float **in_buffers = new float*[in_data_size];
		float **out_buffers = new float*[out_data_size];

		for (int i = 0; i < in_data_size; ++i) 
			in_buffers[i] = new float[jack_get_buffer_size(client)];

		for (int i = 0; i < out_data_size; ++i) 
			out_buffers[i] = new float[jack_get_buffer_size(client)];
		
		std::cout << "running (press ctrl-c to quit)..." << std::endl;
		while (!quit) {
			convolution_process(&conv, in_buffers, out_buffers, gain, min_bin, max_bin);
		}
	}
	else {
	
		// std::cout << chunks.size() << std::endl;
		jack_set_process_callback(client, process, 0);
		jack_activate(client);
		std::cout << "running (press ctrl-c to quit)..." << std::endl;	
	}

	while (!quit) {sleep (1);}	

	jack_deactivate(client);
	jack_client_close(client);

	convolution_destroy(&conv);
}
