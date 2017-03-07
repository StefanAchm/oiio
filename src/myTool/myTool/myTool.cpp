#include <iostream>
#include "stdafx.h"

#include <imageio.h>


OIIO_NAMESPACE_USING
//using namespace std;

void main() {

	std::string filename;

	std::cout << "Give a file you want to import";
	std::cin >> filename;


	ImageInput *in = ImageInput::open(filename);

	if (!in)
		return;

	const ImageSpec &spec = in->spec();
	int xres = spec.width;
	int yres = spec.height;
	int channels = spec.nchannels;
	std::vector<unsigned char> pixels(xres*yres*channels);
	//in->read_image(TypeDesc::UINT8, &pixels[0]);
	in->close();
	ImageInput::destroy(in);

	printf("Channels: ", &channels);

}