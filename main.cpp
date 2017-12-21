#include <iostream>
#include <vector>
#include <algorithm>
#include <regex>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <highfive/H5File.hpp>
#include <chrono>
#include <uWS/uWS.h>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/pointer.h"
#include "rapidjson/prettywriter.h"
#include <zfp.h>

using namespace std;
using namespace HighFive;
using namespace rapidjson;


mutex eventMutex;

vector<vector<float>> dataCache;

File* file = nullptr;

DataSet* dataSet = nullptr;

string baseFolder = "/home/angus";

string currentFileName = "";

int currentBand = -1;

int numBands = -1;

struct ReadRegionRequest
{
	int x, y, w, h, band, mip, compression;
};

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
	if (regionQuery.x < 0 || regionQuery.y < 0 || regionQuery.band < 0 || regionQuery.band >= numBands || regionQuery.mip < 1 || regionQuery.w < 1 || regionQuery.h < 1)
		return false;
	return true;
}

bool loadBand(int band)
{
	if (!file)
	{
		fmt::print("No file loaded\n");
		return false;
	}

	if (dataSet)
	{
		delete dataSet;
		dataSet = nullptr;
	}

	try
	{
		string groupName = fmt::format("Image{0:03d}", band);
		Group group = file->getGroup(groupName);
		DataSet newDataset = group.getDataSet("Data");
		dataSet = new DataSet(newDataset);
		auto dims = dataSet->getSpace().getDimensions();
		if (dims.size() != 2)
			return false;
	}
	catch (HighFive::Exception& err)
	{
		fmt::print("Invalid band or bad band group structure for band {} in file {}\n", band, currentFileName);
		return false;
	}
	currentBand = band;
	return true;
}

bool loadFile(const string& filename, int defaultBand = 0)
{
	if (filename == currentFileName)
		return true;
	if (file)
	{
		delete (file);
		file = nullptr;
	}
	try
	{
		file = new File(filename, File::ReadOnly);
		vector<string> fileObjectList = file->listObjectNames();
		regex imageGroupRegex("Image\\d+");
		numBands = int(std::count_if(fileObjectList.begin(), fileObjectList.end(), [imageGroupRegex](string s)
		{ return regex_search(s, imageGroupRegex) > 0; }));
		currentFileName = filename;

		return loadBand(defaultBand);
	}
	catch (HighFive::Exception& err)
	{
		fmt::print("Problem loading file {}\n", currentFileName);
		return false;
	}
}

bool readRegion(const ReadRegionRequest& req)
{
	if (currentBand != req.band)
	{
		if (!loadBand(req.band))
		{
			fmt::print("Select band {} is invalid!\n", req.band);
			return false;
		}
	}

	DataSpace dataSpace = dataSet->getSpace();
	auto dims = dataSpace.getDimensions();
	if (dims.size() != 2 || dims[0] < req.y + req.h || dims[1] < req.x + req.w)
	{
		fmt::print("Selected region ({}, {}) -> ({}, {} in band {} is invalid!\n", req.x, req.y, req.x + req.w, req.x + req.h, req.band);
		return false;
	}

	dataSet->select({req.y, req.x}, {req.h/req.mip, req.w/req.mip}, {req.mip, req.mip}).read(dataCache);
	return true;
}

void onRegionRead(uWS::WebSocket<uWS::SERVER>* ws, const Value& message)
{
	eventMutex.lock();
	ReadRegionRequest request;

	if (parseRegionQuery(message, request))
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		bool compressed = request.compression>=4 && request.compression<32;
		if (readRegion(request))
		{
			auto tEnd = std::chrono::high_resolution_clock::now();
			auto dtRegion = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
			auto numValues = dataCache.size() * dataCache[0].size();

			Document d;
			Pointer("/event").Set(d, "region_read");
			Pointer("/message/success").Set(d, true);
			Pointer("/message/compression").Set(d, request.compression);
			Pointer("/message/x").Set(d, request.x);
			Pointer("/message/y").Set(d, request.y);
			Pointer("/message/w").Set(d, dataCache[0].size());
			Pointer("/message/h").Set(d, dataCache.size());
			Pointer("/message/mip").Set(d, request.mip);
			Pointer("/message/band").Set(d, request.band);
			Pointer("/message/numValues").Set(d, numValues);

			tStart = std::chrono::high_resolution_clock::now();
			auto dataPayload = new float[dataCache[0].size() * dataCache.size()];
			size_t numRows = dataCache.size();
			size_t rowLength = dataCache[0].size();
			float* currentPos = dataPayload;
			for (auto& row: dataCache)
			{
				memcpy(currentPos, row.data(), rowLength * sizeof(float));
				currentPos += rowLength;
			}
			tEnd = std::chrono::high_resolution_clock::now();
			auto dtPayload = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

			tStart = std::chrono::high_resolution_clock::now();
			if (compressed)
			{
				size_t compressedSize;
				unsigned char* compressionBuffer;
				compress(dataPayload, compressionBuffer, compressedSize, rowLength, numRows, request.compression);
				//decompress(dataPayload, compressionBuffer, compressedSize, rowLength, numRows, request.compression);

				tEnd = std::chrono::high_resolution_clock::now();
				eventMutex.unlock();
				sendEventBinaryPayload(ws, d, compressionBuffer, compressedSize);
				delete[] compressionBuffer;
				delete[] dataPayload;
				auto dtCompress = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

				fmt::print("Compressed binary ({:.3f} MB) sent in in {} ms\n", compressedSize/1e6, dtCompress);
			}
			else
			{
				eventMutex.unlock();
				sendEventBinaryPayload(ws, d, dataPayload, numRows * rowLength * sizeof(float));
				delete[] dataPayload;
				tEnd = std::chrono::high_resolution_clock::now();
				auto dtSent = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
				fmt::print("Uncompressed binary ({:.3f} MB) sent in in {} ms\n", numRows * rowLength * sizeof(float)/1e6, dtSent);

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
			Pointer("/message/numBands").Set(d, numBands);
			Pointer("/message/success").Set(d, true);
			Pointer("/event").Set(d, "fileload");
			eventMutex.unlock();
			sendEvent(ws, d);
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

//void OnConnected()
//{
//	fmt::print("Connected\n");
//}
//
//void OnReadRequest(sio::event& ev)
//{
//	auto data = ev.get_message();
//	int64_t band = data->get_map()["band"]->get_int();
//	int64_t x = data->get_map()["x"]->get_int();
//	int64_t y = data->get_map()["y"]->get_int();
//	int64_t w = data->get_map()["w"]->get_int();
//	int64_t h = data->get_map()["h"]->get_int();
//	auto tStart = std::chrono::high_resolution_clock::now();
//	readRegion(band, x, y, w, h);
//	auto tEnd = std::chrono::high_resolution_clock::now();
//	auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd-tStart).count();
//	fmt::print("Responded to read request [band{}, ({}, {}) -> ({}, {})] in {} ms\n", band, x, y, x + w, x + h, dt);
//}
//
//int main(int argc, char** argv)
//{
//	file = new File("/home/angus/GALFACTS_N4_0263_4023_10chanavg_I.hdf5", File::ReadOnly);
//	vector<string> fileObjectList = file->listObjectNames();
//	regex imageGroupRegex("Image\\d+");
//	auto numBands = std::count_if(fileObjectList.begin(), fileObjectList.end(), [imageGroupRegex](string s)
//	{ return regex_search(s, imageGroupRegex) > 0; });
//
//	vector<vector<float>> result;
//	uWS::Hub h;
//
//	h.onMessage([](uWS::WebSocket<uWS::SERVER> *ws, char *message, size_t length, uWS::OpCode opCode) {
//		ws->send(message, length, opCode);
//	});
//
//	h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data, size_t length, size_t remainingBytes) {
//		res->end(const char *, size_t);
//	});
//
//	if (h.listen(3000)) {
//		h.run();
//	}
//	return 0;
//}

