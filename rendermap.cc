/*
 * Copyright (C) 2011 Lukas Niederbremer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <iostream>

#include <png.h>
#include <cppnbt.h>

#include <cassert>

#include <cstring>
#include <cstdlib>
#include <string>

using namespace std;
using namespace nbt;

struct InvalidMapException {
	// Why not.
};

struct MinecraftMap {
	int16_t width;
	int16_t height;
	ByteArray map_data;
};

struct RGBA {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

static std::vector<RGBA> colors_orig{
#  include "color_data.orig.hh"
};

static std::vector<RGBA> colors_1_7{
#  include "color_data.1.7.hh"
};

static std::vector<RGBA> colors_1_8{
#  include "color_data.1.8.hh"
};

static std::vector<RGBA> colors_1_12{
#  include "color_data.1.12.hh"
};

struct PngFile {
	int16_t width;
	int16_t height;
	RGBA* data;
};

void load(NbtFile&, MinecraftMap&);
void write(PngFile&, FILE*);

int main(int argc, char** argv)
{
	uint8_t scale = 4;
	const char* filename = NULL;

	std::vector<RGBA> const* colors = &colors_orig; 

	if (argc < 2) {
		cerr << "Usage: "
			<< argv[0] << " <map_file> [scale] [output] [mapping]" << std::endl;

		cerr << "where" << endl;
		cerr << "\tmap_file: 'Map_X.dat' in 'serverroot/world/data'" << endl;
		cerr << "\tscale   : scale of the map (default '4' [= 1 map pixel "
			"translates to 4 pixel in the output])" << endl;
                cerr << "\toutput  : filename for the png output" << endl;
		cerr << "\tmapping : color mapping to use ('original', '1.7', '1.8' or '1.12')"
			<< endl;

		return 1;
	}

	if (argc > 2) {
		/* We also got a scale, replace default */
		scale = static_cast<uint8_t>(::atoi(argv[2]));
	}

	if (argc > 3) {
		/* Got filename, output there */
		filename = argv[3];
	}

	if (argc > 4) {
		/* Got colormap */
		if (!strcmp(argv[4], "1.7")) {
			colors = &colors_1_7;
		} else if (!strcmp(argv[4], "1.8")) {
			colors = &colors_1_8;
		} else if (!strcmp(argv[4], "1.12")) {
			colors = &colors_1_12;
		} else if (!strcmp(argv[4], "original")) {
			// nop
		} else {
			std::cerr << argv[0] << ": invalid mapping: " << argv[4] << std::endl;
			return 1;
		}
	}

	NbtFile f;
	MinecraftMap m;

	FILE* fp = nullptr;

	if ((filename == nullptr) || !strcmp(filename, "-")) {
		fp = stdout;
	} else {
		fp = fopen(filename, "wb");

		if (!fp) {
			cerr << "Unable to open file '" << filename << "' for writing"
				<< endl;
			return 1;
		}
	}

	try {
		f.open(argv[1]);
		f.read();
		load(f, m);

	} catch(GzipIOException &e) {
		cerr << "Error reading '" << argv[1] << "' (code " << e.getCode() << ")"
			<< endl;
		return 1;

	} catch(InvalidMapException &e) {
		cerr << "Invalid map file!" << endl;
		return 1;
	}

	m.height *= scale;
	m.width *= scale;

	RGBA* pixel_data = new RGBA[m.height * m.width];

	/*
	 * Madness ensues!
	 */
	for (int y = 0; y < m.height; y += scale) {
		for (int s1 = 0; s1 < scale; ++s1) {
			for (int x = 0; x < m.width; x += scale) {
				for (int s2 = 0; s2 < scale; ++s2) {
					auto pixel = m.map_data[(y/scale)*(m.width/scale)+(x/scale)];
					RGBA data;

					if (pixel >= colors->size()) {
						std::cerr << (int)pixel << ": unknown color" << std::endl;
						data = {255, 0, 255, 255}; 
					} else {
						data = (*colors)[pixel];
					}

					pixel_data[(y+s1)*m.width + (x+s2)] = data;
				}
			}
		}
	}

	PngFile pf = { m.width, m.height, pixel_data };
	string basename(argv[1]);

	write(pf, fp);

	delete[] pixel_data;

	return 0;
}

void write(PngFile& f, FILE* fp)
{
	png_bytep* row_pointers;
	png_structp png_ptr;
	png_infop info_ptr;

	png_byte color_type = PNG_COLOR_TYPE_RGBA;
	png_byte bit_depth  = 8;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png_ptr) {
		cerr << "write(): unable to create write struct" << endl;
		goto cleanup_pre_write;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		cerr << "write(): unable to create info struct" << endl;
		goto cleanup_pre_info;
	}

	png_init_io(png_ptr, fp);

	if (setjmp(png_jmpbuf(png_ptr))) {
		cerr << "write(): error while writing header" << endl;
		goto cleanup_pre_alloc;
	}

	png_set_IHDR(png_ptr, info_ptr, f.width, f.height, bit_depth, color_type,
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
			PNG_FILTER_TYPE_BASE);
	png_write_info(png_ptr, info_ptr);

	row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * f.height);
	if (!row_pointers) {
		cerr << "write(): Unable to allocate memory for row pointers" << endl;
		goto cleanup_pre_alloc;
	}

	/*
	 * Set everything to 0 so we can spam "free()" over the whole list even if
	 * parts of it where unable to allocate
	 */
	memset(row_pointers, 0, sizeof(png_bytep) * f.height);

	for (int y = 0; y < f.height; ++y) {
		row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png_ptr,info_ptr));

		if (!row_pointers[y]) {
			cerr << "write(): Unable to allocate row pointer[" << y << "]"
				<< endl;
			goto cleanup_post_alloc; // At least the first alloc succeeded
		}
	}

	for (int y = 0; y < f.height; ++y) {
		png_byte* row = row_pointers[y];

		for (int x = 0; x < f.width; ++x) {
			png_byte* ptr = &(row[x*4]);

			RGBA* color = &(f.data[y*f.width + x]);

			ptr[0] = color->r;
			ptr[1] = color->g;
			ptr[2] = color->b;
			ptr[3] = color->a;
		}
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		cerr << "write(): error while writing image data" << endl;
		goto cleanup_post_alloc;
	}

	png_write_image(png_ptr, row_pointers);

	if (setjmp(png_jmpbuf(png_ptr))) {
		cerr << "write(): error while writing end" << endl;
		goto cleanup_post_alloc;
	}

	png_write_end(png_ptr, nullptr);

cleanup_post_alloc:
	for (int y = 0; y < f.height; ++y)
		free(row_pointers[y]);

	free(row_pointers);
cleanup_pre_alloc:
cleanup_pre_info:
	png_destroy_write_struct(&png_ptr, &info_ptr);
cleanup_pre_write:
	fclose(fp);

	return;
}

void load(NbtFile& f, MinecraftMap& m)
{
	try {
		TagCompound* file_root = dynamic_cast<TagCompound*>(f.getRoot());

		if (!file_root) {
			throw InvalidMapException();
		}

		TagCompound* map_data = dynamic_cast<TagCompound*>(
				file_root->getValueAt("data"));

		if (!map_data) {
			throw InvalidMapException();
		}

		TagShort* height = dynamic_cast<TagShort*>(
				map_data->getValueAt("height"));
		TagShort* width = dynamic_cast<TagShort*>(
				map_data->getValueAt("width"));
		TagByteArray* data = dynamic_cast<TagByteArray*>(
				map_data->getValueAt("colors"));

		if (!width || !height || !data) {
			throw InvalidMapException();
		}

		m.height = height->getValue();
		m.width  = width->getValue();
		m.map_data = data->getValue();

	} catch (KeyNotFoundException& e) {
		throw InvalidMapException();
	}

	return;
}
