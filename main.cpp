#include <iostream>
#include <vector>
#include <algorithm>
#include <regex>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <highfive/H5File.hpp>
#include <chrono>
#include <limits>
#include <future>
#include <random>
#include <uWS/uWS.h>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/pointer.h"
#include "rapidjson/prettywriter.h"
#include <zfp.h>

using namespace std;
using namespace HighFive;
using namespace rapidjson;


struct ImageFile
{
	string filename;
	int numBands;
	int width;
	int height;
	File* file = nullptr;
	vector<Group> groups;
	vector<DataSet> dataSets;
};

struct RegionReadResponse
{
	bool success;
	int compression;
	int x;
	int y;
	int w;
	int h;
	int mip;
	int band;
	int numValues;
};

struct ReadRegionRequest
{
	int x, y, w, h, band, mip, compression;
};

struct Histogram
{
	int N;
	float firstBinCenter, binWidth;
	vector<int> bins;
};

mutex eventMutex;

vector<vector<float>> currentBandCache;

Histogram currentBandHistogram;

ImageFile imageFile;

string baseFolder = "/home/angus";

int currentBand = -1;

Histogram getHistogram(vector<vector<float>>& values)
{
	Histogram histogram;

	auto numRows = values.size();
	if (!numRows)
		return histogram;
	auto rowSize = values[0].size();
	if (!rowSize)
		return histogram;
	float minVal = values[0][0];
	float maxVal = values[0][0];

	for (auto& row:values)
	{
		for (auto& v:row)
		{
			minVal = fmin(minVal, v);
			maxVal = fmax(maxVal, v);
		}
	}

	histogram.N = max(sqrt(values.size()), 2.0);
	histogram.N = 1000;
	histogram.binWidth = (maxVal - minVal) / histogram.N;
	histogram.firstBinCenter = minVal + histogram.binWidth / 2.0f;
	histogram.bins.resize(histogram.N, 0);
	for (auto& row:values)
	{
		for (auto& v:row)
		{
			if (isnan(v))
				continue;
			int bin = min((int) ((v - minVal) / histogram.binWidth), histogram.N);
			histogram.bins[bin]++;
		}
	}
	return histogram;
}

int compress(float* array, unsigned char*& compressionBuffer, size_t& zfpsize, uint nx, uint ny, uint precision)
{
	int status = 0;    /* return value: 0 = success */
	zfp_type type;     /* array scalar type */
	zfp_field* field;  /* array meta data */
	zfp_stream* zfp;   /* compressed stream */
	size_t bufsize;    /* byte size of compressed buffer */
	bitstream* stream; /* bit stream to write to or read from */

	type = zfp_type_float;
	field = zfp_field_2d(array, type, nx, ny);

	/* allocate meta data for a compressed stream */
	zfp = zfp_stream_open(nullptr);

	/* set compression mode and parameters via one of three functions */
	zfp_stream_set_precision(zfp, precision);

	/* allocate buffer for compressed data */
	bufsize = zfp_stream_maximum_size(zfp, field);
	compressionBuffer = new unsigned char[bufsize];
	/* associate bit stream with allocated buffer */
	stream = stream_open(compressionBuffer, bufsize);
	zfp_stream_set_bit_stream(zfp, stream);
	zfp_stream_rewind(zfp);

	zfpsize = zfp_compress(zfp, field);
	if (!zfpsize)
	{
		status = 1;
	}

	/* clean up */
	zfp_field_free(field);
	zfp_stream_close(zfp);
	stream_close(stream);

	return bufsize;
}

int decompress(float* array, unsigned char* compressionBuffer, size_t& zfpsize, uint nx, uint ny, uint precision)
{
	int status = 0;    /* return value: 0 = success */
	zfp_type type;     /* array scalar type */
	zfp_field* field;  /* array meta data */
	zfp_stream* zfp;   /* compressed stream */
	bitstream* stream; /* bit stream to write to or read from */

	/* allocate meta data for the 3D array a[nz][ny][nx] */
	type = zfp_type_float;
	field = zfp_field_2d(array, type, nx, ny);

	/* allocate meta data for a compressed stream */
	zfp = zfp_stream_open(NULL);
	zfp_stream_set_precision(zfp, precision);

	stream = stream_open(compressionBuffer, zfpsize);
	zfp_stream_set_bit_stream(zfp, stream);
	zfp_stream_rewind(zfp);

	if (!zfp_decompress(zfp, field))
	{
		fprintf(stderr, "decompression failed\n");
		status = 1;
	}
	/* clean up */
	zfp_field_free(field);
	zfp_stream_close(zfp);
	stream_close(stream);

	return status;
}

vector<int32_t> getNanEncodings(float* array, size_t length)
{
	int32_t prevIndex = 0;
	bool prev = false;
	vector<int32_t> encodedArray;

	// Find first non-NaN number in the array
	float prevValidNum = 0;
	for (auto i = 0; i < length; i++)
	{
		if (!isnan(array[i]))
		{
			prevValidNum = array[i];
			break;
		}
	}

	for (auto i = 0; i < length; i++)
	{
		bool current = isnan(array[i]);
		if (current != prev)
		{
			encodedArray.push_back(i - prevIndex);
			prevIndex = i;
			prev = current;
		}
		if (current)
		{
			array[i] = prevValidNum;
		}
		else
		{
			prevValidNum = array[i];
		}
	}
	encodedArray.push_back(length - prevIndex);
	return encodedArray;
}

void sendEvent(uWS::WebSocket<uWS::SERVER>* ws, Document& document)
{
	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	document.Accept(writer);
	string jsonPayload = buffer.GetString();
	ws->send(jsonPayload.c_str(), jsonPayload.size(), uWS::TEXT);
}

void sendEventBinaryPayload(uWS::WebSocket<uWS::SERVER>* ws, Document& document, void* payload, uint32_t length)
{
	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	document.Accept(writer);
	string jsonPayload = buffer.GetString();
	auto rawData = new char[jsonPayload.size() + length + sizeof(length)];
	memcpy(rawData, &length, sizeof(length));
	memcpy(rawData + sizeof(length), payload, length);
	memcpy(rawData + length + sizeof(length), jsonPayload.c_str(), jsonPayload.size());
	ws->send(rawData, jsonPayload.size() + length + sizeof(length), uWS::BINARY);
	delete[] rawData;
}

bool parseRegionQuery(const Value& message, ReadRegionRequest& regionQuery)
{
	const char* intVarNames[] = {"x", "y", "w", "h", "band", "mip", "compression"};

	for (auto varName:intVarNames)
	{
		if (!message.HasMember(varName) || !message[varName].IsInt())
			return false;
	}

	regionQuery = {message["x"].GetInt(), message["y"].GetInt(), message["w"].GetInt(), message["h"].GetInt(), message["band"].GetInt(), message["mip"].GetInt(), message["compression"].GetInt()};
	if (regionQuery.x < 0 || regionQuery.y < 0 || regionQuery.band < 0 || regionQuery.band >= imageFile.numBands || regionQuery.mip < 1 || regionQuery.w < 1 || regionQuery.h < 1)
		return false;
	return true;
}

bool loadBand(int band)
{
	if (!imageFile.file)
	{
		fmt::print("No file loaded\n");
		return false;
	}
	else if (band >= imageFile.dataSets.size())
	{
		fmt::print("Invalid band for band {} in file {}\n", band, imageFile.filename);
		return false;
	}

	imageFile.dataSets[band].read(currentBandCache);
	currentBandHistogram = getHistogram(currentBandCache);
	currentBand = band;
	return true;
}

bool loadFile(const string& filename, int defaultBand = 0)
{
	if (filename == imageFile.filename)
		return true;
	if (imageFile.file)
		delete imageFile.file;
	try
	{
		imageFile.file = new File(filename, File::ReadOnly);
		vector<string> fileObjectList = imageFile.file->listObjectNames();
		regex imageGroupRegex("Image\\d+");
		imageFile.numBands = int(std::count_if(fileObjectList.begin(), fileObjectList.end(), [imageGroupRegex](string s)
		{ return regex_search(s, imageGroupRegex) > 0; }));
		imageFile.filename = filename;


		for (auto i = 0; i < imageFile.numBands; i++)
		{
			string groupName = fmt::format("Image{0:03d}", i);
			Group group = imageFile.file->getGroup(groupName);
			DataSet dataSet = group.getDataSet("Data");
			auto dims = dataSet.getSpace().getDimensions();
			if (dims.size() != 2)
			{
				fmt::print("Problem loading file {}: Data set for group {} is not a valid 2D array.\n", filename, groupName);
				return false;
			}

			if (i == 0)
			{
				imageFile.width = dims[1];
				imageFile.height = dims[0];
			}
			else if (dims[1] != imageFile.width || dims[0] != imageFile.height)
			{
				fmt::print("Problem loading file {}: Data set for group {} has mismatched dimensions.\n", filename, groupName);
				return false;
			}

			imageFile.dataSets.push_back(dataSet);
			imageFile.groups.push_back(group);
		}
		return loadBand(defaultBand);
	}
	catch (HighFive::Exception& err)
	{
		fmt::print("Problem loading file {}\n", filename);
		return false;
	}
}

vector<float> getZProfile(int x, int y)
{
	if (!imageFile.file)
	{
		fmt::print("No file loaded\n");
		return vector<float>();
	}
	else if (x >= imageFile.width || y >= imageFile.height)
	{
		fmt::print("Z profile out of range\n");
		return vector<float>();
	}

	//try
	{
		vector<float> profile;
		profile.resize(imageFile.numBands);
		vector<future<float>> futures;

		for (auto i = 0; i < imageFile.numBands; i++)
		{
			futures.emplace_back(async(launch::async, [imageFile, i, x, y]()
			{
				vector<float> val;
				string groupName = fmt::format("Image{0:03d}", i);
				Group group = imageFile.file->getGroup(groupName);
				DataSet dataSet = group.getDataSet("Data");
				dataSet.select({y, x}, {1, 1}).read(val);
				return val[0];
			}));
		}

		for (auto i = 0; i < imageFile.numBands; i++)
			profile[i] = futures[i].get();
		return profile;
	}
//	catch (HighFive::Exception& err)
//	{
//		fmt::print("Invalid profile request in file {}\n", imageFile.filename);
//		return vector<float>();
//	}
}

vector<float> readRegion(const ReadRegionRequest& req)
{
	if (currentBand != req.band)
	{
		if (!loadBand(req.band))
		{
			fmt::print("Select band {} is invalid!\n", req.band);
			return vector<float>();
		}
	}

	size_t numRowsBand = currentBandCache.size();
	size_t rowLengthBand = currentBandCache[0].size();

	if (numRowsBand < req.y + req.h || rowLengthBand < req.x + req.w)
	{
		fmt::print("Selected region ({}, {}) -> ({}, {} in band {} is invalid!\n", req.x, req.y, req.x + req.w, req.x + req.h, req.band);
		return vector<float>();
	}

	size_t numRowsRegion = req.h / req.mip;
	size_t rowLengthRegion = req.w / req.mip;
	vector<float> regionData;
	regionData.resize(numRowsRegion * rowLengthRegion);

	for (auto j = 0; j < numRowsRegion; j++)
	{
		for (auto i = 0; i < rowLengthRegion; i++)
		{
			float sumPixel = 0;
			int count = 0;
			for (auto x = 0; x < req.mip; x++)
			{
				for (auto y = 0; y < req.mip; y++)
				{
					float pixVal = currentBandCache[req.y + j * req.mip + y][req.x + i * req.mip + x];
					if (!isnan(pixVal))
					{
						count++;
						sumPixel += pixVal;
					}
				}
			}
			regionData[j * rowLengthRegion + i] = count ? sumPixel / count : NAN;
		}
	}
	return regionData;
}

void onRegionRead(uWS::WebSocket<uWS::SERVER>* ws, const Value& message)
{
	eventMutex.lock();
	ReadRegionRequest request;

	if (parseRegionQuery(message, request))
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		bool compressed = request.compression >= 4 && request.compression < 32;
		vector<float> regionData = readRegion(request);
		if (regionData.size())
		{
			auto tEnd = std::chrono::high_resolution_clock::now();
			auto dtRegion = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
			auto numValues = regionData.size();
			auto rowLength = request.w / request.mip;
			auto numRows = request.h / request.mip;

			Document d(kObjectType);
			auto& a = d.GetAllocator();
			d.AddMember("event", "region_read", d.GetAllocator());

			Value responseMessage(kObjectType);
			responseMessage.AddMember("success", true, d.GetAllocator());
			responseMessage.AddMember("compression", request.compression, a);
			responseMessage.AddMember("x", request.x, a);
			responseMessage.AddMember("y", request.y, a);
			responseMessage.AddMember("w", rowLength, a);
			responseMessage.AddMember("h", numRows, a);
			responseMessage.AddMember("mip", request.mip, a);
			responseMessage.AddMember("band", request.band, a);
			responseMessage.AddMember("numValues", numValues, a);

			Value hist(kObjectType);
			hist.AddMember("firstBinCenter", currentBandHistogram.firstBinCenter, a);
			hist.AddMember("binWidth", currentBandHistogram.binWidth, a);
			hist.AddMember("N", currentBandHistogram.N, a);

			Value binsValue(kArrayType);
			binsValue.Reserve(currentBandHistogram.N, a);
			for (auto& v: currentBandHistogram.bins)
				binsValue.PushBack(v, a);

			hist.AddMember("bins", binsValue, a);
			responseMessage.AddMember("hist", hist, a);
			d.AddMember("message", responseMessage, a);

			//d["message/success"].SetBool(true);
			//d["message/compression"].SetInt(request.compression);
			//Pointer("/event").Set(d, "region_read");
			//Pointer("/message/success").Set(d, true);
			//Pointer("/message/compression").Set(d, request.compression);
//			Pointer("/message/x").Set(d, request.x);
//			Pointer("/message/y").Set(d, request.y);
//			Pointer("/message/w").Set(d, rowLength);
//			Pointer("/message/h").Set(d, numRows);
//			Pointer("/message/mip").Set(d, request.mip);
//			Pointer("/message/band").Set(d, request.band);
//			Pointer("/message/numValues").Set(d, numValues);
			//Pointer("/message/hist").Set(d, histVals);

			tStart = std::chrono::high_resolution_clock::now();

			tEnd = std::chrono::high_resolution_clock::now();
			auto dtPayload = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

			tStart = std::chrono::high_resolution_clock::now();
			if (compressed)
			{
				auto nanEncoding = getNanEncodings(regionData.data(), regionData.size());
				size_t compressedSize;
				unsigned char* compressionBuffer;
				compress(regionData.data(), compressionBuffer, compressedSize, rowLength, numRows, request.compression);
				//decompress(dataPayload, compressionBuffer, compressedSize, rowLength, numRows, request.compression);

				char* binaryPayload = new char[rowLength * numRows];
				int32_t numNanEncodings = nanEncoding.size();
				memcpy(binaryPayload, &numNanEncodings, sizeof(int32_t));
				memcpy(binaryPayload + sizeof(int32_t), nanEncoding.data(), sizeof(int32_t) * numNanEncodings);
				memcpy(binaryPayload + sizeof(int32_t) + sizeof(int32_t) * numNanEncodings, compressionBuffer, compressedSize);
				uint32_t payloadSize = sizeof(int32_t) + sizeof(int32_t) * numNanEncodings + compressedSize;
				tEnd = std::chrono::high_resolution_clock::now();
				eventMutex.unlock();
				sendEventBinaryPayload(ws, d, binaryPayload, payloadSize);
				delete[] compressionBuffer;
				delete[] binaryPayload;
				auto dtCompress = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

				fmt::print("Compressed binary ({:.3f} MB) sent in in {} ms\n", compressedSize / 1e6, dtCompress);
			}
			else
			{
				eventMutex.unlock();
				sendEventBinaryPayload(ws, d, regionData.data(), numRows * rowLength * sizeof(float));
				tEnd = std::chrono::high_resolution_clock::now();
				auto dtSent = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
				fmt::print("Uncompressed binary ({:.3f} MB) sent in in {} ms\n", numRows * rowLength * sizeof(float) / 1e6, dtSent);

			}
			return;
		}
		else
		{
			fmt::print("ReadRegion request is out of bounds\n");
		}
	}
	fmt::print("Event is not a valid ReadRegion request !\n");

	Document d;
	Pointer("/event").Set(d, "region_read");
	Pointer("/message/success").Set(d, false);
	eventMutex.unlock();
	sendEvent(ws, d);
}

void onFileLoad(uWS::WebSocket<uWS::SERVER>* ws, const Value& message)
{
	eventMutex.lock();
	if (message.HasMember("filename") && message["filename"].IsString())
	{
		string filename = message["filename"].GetString();
		if (loadFile(fmt::format("{}/{}", baseFolder, filename)))
		{
			fmt::print("File {} loaded successfully\n", filename);
			Document d;
			Pointer("/message/numBands").Set(d, imageFile.numBands);
			Pointer("/message/success").Set(d, true);
			Pointer("/event").Set(d, "fileload");
			eventMutex.unlock();
			sendEvent(ws, d);
			vector<float> zProfile;

			vector<float> readTimes;

			srand (time(NULL));
			for (auto i =0; i< 100;i++)
			{
				auto tStart = std::chrono::high_resolution_clock::now();
				int randX = ((float) rand()) / RAND_MAX * imageFile.width;
				int randY = ((float) rand()) / RAND_MAX * imageFile.height;
				zProfile = getZProfile(randX, randY);
				auto tEnd = std::chrono::high_resolution_clock::now();
				auto dtZProfile = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
				readTimes.push_back(dtZProfile);
			}

			float sumX = 0;
			float sumX2 = 0;
			float minVal = readTimes[0];
			float maxVal = readTimes[0];

			for (auto& dt: readTimes)
			{
				sumX+=dt;
				sumX2+=dt*dt;
				minVal = min(minVal, dt);
				maxVal = max(maxVal, dt);
			}

			float mean = sumX/readTimes.size();
			float sigma = sqrt(sumX2/readTimes.size() - mean*mean);
			fmt::print("Z Profile reads: N={}; mean={} ms; sigma={} ms; Range: {} -> {} ms\n", readTimes.size(), mean, sigma, minVal, maxVal);


			return;
		}
		else
		{
			fmt::print("Error loading file {}\n", filename);
		}
	}

	Document d;
	Pointer("/event").Set(d, "fileload");
	Pointer("/message/success").Set(d, false);
	eventMutex.unlock();
	sendEvent(ws, d);
}

void onMessage(uWS::WebSocket<uWS::SERVER>* ws, char* rawMessage, size_t length, uWS::OpCode opCode)
{
	if (opCode == uWS::OpCode::TEXT)
	{
		char* paddedMessage = new char[length + 1];
		memcpy(paddedMessage, rawMessage, length);
		paddedMessage[length] = 0;

		Document d;
		d.Parse(paddedMessage);
		delete[] paddedMessage;

		StringBuffer buffer;
		PrettyWriter<StringBuffer> writer(buffer);
		d.Accept(writer);

		if (d.HasMember("event") && d.HasMember("message") && d["message"].IsObject())
		{
			string eventName(d["event"].GetString());
			Value& message = GetValueByPointerWithDefault(d, "/message", "{}");

			if (eventName == "region_read")
			{
				onRegionRead(ws, message);
			}
			else if (eventName == "fileload")
			{
				onFileLoad(ws, message);
			}
			else
			{
				fmt::print("Unknown query type!\n");
			}

		}
		else
			fmt::print("Missing event or message parameters\n");
	}
	else if (opCode == uWS::OpCode::BINARY)
		fmt::print("Binary recieved ({} bytes)\n", length);

	//ws->send(rawMessage, length, opCode);
};

int main()
{
	uWS::Hub h;

	h.onMessage(&onMessage);
	if (h.listen(3002))
	{
		h.run();
	}
}