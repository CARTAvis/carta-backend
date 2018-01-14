#include <iostream>
#include <vector>
#include <algorithm>
#include <regex>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <boost/multi_array.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
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

typedef boost::multi_array<float, 3> Matrix3F;
typedef boost::multi_array<float, 2> Matrix2F;



struct ImageFile
{
	string filename;
	int numBands;
	int width;
	int height;
	File* file = nullptr;
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


struct SessionInfo
{
	Matrix3F currentBandCache;
	Histogram* currentBandHistogram ;
	int currentBand;
	ImageFile imageFile;
	mutex eventMutex;
	boost::uuids::uuid uuid;

public:
	SessionInfo()
		:uuid(boost::uuids::random_generator()()),
		 currentBandHistogram(nullptr),
		 currentBand(-1)
	{

	}
};


map<uWS::WebSocket<uWS::SERVER>*, SessionInfo*> sessions;

string baseFolder = "/home/angus";


Histogram* getHistogram(const SessionInfo* session, Matrix3F& values)
{
	if (!session)
		return nullptr;
	auto numRows = session->imageFile.height;
	if (!numRows)
		return nullptr;
	auto rowSize = session->imageFile.width;
	if (!rowSize)
		return nullptr;

	Histogram* histogram = new Histogram;
	float minVal = values[0][0][0];
	float maxVal = values[0][0][0];

	for (auto i =0; i<session->imageFile.height;i++)
	{
		for (auto j=0;j<session->imageFile.width;j++)
		{
			minVal = fmin(minVal, values[0][i][j]);
			maxVal = fmax(maxVal, values[0][i][j]);
		}
	}

	histogram->N = max(sqrt(session->imageFile.width*session->imageFile.height), 2.0);
	histogram->binWidth = (maxVal - minVal) / histogram->N;
	histogram->firstBinCenter = minVal + histogram->binWidth / 2.0f;
	histogram->bins.resize(histogram->N, 0);
	for (auto i =0; i<session->imageFile.height;i++)
	{
		for (auto j=0;j<session->imageFile.width;j++)
		{
			auto v = values[0][i][j];
			if (isnan(v))
				continue;
			int bin = min((int) ((v - minVal) / histogram->binWidth), histogram->N-1);
			histogram->bins[bin]++;
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

bool parseRegionQuery(const SessionInfo* session, const Value& message, ReadRegionRequest& regionQuery)
{
	if (!session)
		return false;
	const char* intVarNames[] = {"x", "y", "w", "h", "band", "mip", "compression"};

	for (auto varName:intVarNames)
	{
		if (!message.HasMember(varName) || !message[varName].IsInt())
			return false;
	}

	regionQuery = {message["x"].GetInt(), message["y"].GetInt(), message["w"].GetInt(), message["h"].GetInt(), message["band"].GetInt(), message["mip"].GetInt(), message["compression"].GetInt()};
	if (regionQuery.x < 0 || regionQuery.y < 0 || regionQuery.band < 0 || regionQuery.band >= session->imageFile.numBands || regionQuery.mip < 1 || regionQuery.w < 1 || regionQuery.h < 1)
		return false;
	return true;
}

bool loadBand(SessionInfo* session, int band)
{
	if (!session)
		return false;
	
	if (!session->imageFile.file)
	{
		fmt::print("Session {}: No file loaded\n", session->uuid);
		return false;
	}
	else if (band >= session->imageFile.numBands)
	{
		fmt::print("Session {}: Invalid band for band {} in file {}\n", session->uuid, band, session->imageFile.filename);
		return false;
	}

	session->imageFile.dataSets[0].select({band, 0, 0}, {1, session->imageFile.height, session->imageFile.width}).read(session->currentBandCache);
	delete session->currentBandHistogram;
	session->currentBandHistogram = getHistogram(session, session->currentBandCache);
	session->currentBand = band;
	return true;
}

bool loadFile(SessionInfo* session, const string& filename, int defaultBand = 0)
{
	if (!session)
		return false;
	if (filename == session->imageFile.filename)
		return true;

	delete session->imageFile.file;

	try
	{
		session->imageFile.file = new File(filename, File::ReadOnly);
		vector<string> fileObjectList = session->imageFile.file->listObjectNames();
		session->imageFile.filename = filename;
		auto group = session->imageFile.file->getGroup("Image");
		DataSet dataSet = group.getDataSet("Data");

		auto dims = dataSet.getSpace().getDimensions();
		if (dims.size() != 3)
		{
			fmt::print("Session {}: Problem loading file {}: Data is not a valid 3D array.\n", session->uuid, filename);
			return false;
		}

		session->imageFile.numBands = dims[0];
		session->imageFile.height = dims[1];
		session->imageFile.width = dims[2];
		session->imageFile.dataSets.clear();
		session->imageFile.dataSets.emplace_back(dataSet);

		if (group.exist("DataSwizzled"))
		{
			DataSet dataSetSwizzled = group.getDataSet("DataSwizzled");
			auto swizzledDims = dataSetSwizzled.getSpace().getDimensions();
			if (swizzledDims.size() != 3 || swizzledDims[0] != dims[2])
			{
				fmt::print("Session {}: Invalid swizzled data set in file {}, ignoring.\n", session->uuid, filename);
			}
			else
			{
				fmt::print("Session {}: Found valid swizzled data set in file {}.\n", session->uuid, filename);
				session->imageFile.dataSets.emplace_back(dataSetSwizzled);
			}
		}
		else
		{
			fmt::print("Session {}: File {} missing optional swizzled data set, using fallback calculation.\n", session->uuid, filename);

		}
		return loadBand(session, defaultBand);
	}
	catch (HighFive::Exception& err)
	{
		fmt::print("Session {}: Problem loading file {}\n", session->uuid, filename);
		return false;
	}
}

vector<float> getZProfile(const SessionInfo* session, int x, int y)
{
	if (!session || !session->imageFile.file)
	{
		fmt::print("No file loaded or invalid session\n");
		return vector<float>();
	}
	else if (x >= session->imageFile.width || y >= session->imageFile.height)
	{
		fmt::print("Session {}: Z profile out of range\n", session->uuid);
		return vector<float>();
	}

	try
	{
		vector<float> profile;

		if (session->imageFile.dataSets.size()==2)
		{
			Matrix3F zP;
			session->imageFile.dataSets[1].select({x, y, 0}, {1, 1, session->imageFile.numBands}).read(zP);
			profile.resize(session->imageFile.numBands);
			memcpy(profile.data(), zP.data(), session->imageFile.numBands*sizeof(float));
		}
		else
		{
			session->imageFile.dataSets[0].select({0, y, x}, {session->imageFile.numBands, 1, 1}).read(profile);
		}
		return profile;
	}
	catch (HighFive::Exception& err)
	{
		fmt::print("Session {}: Invalid profile request in file {}\n", session->uuid, session->imageFile.filename);
		return vector<float>();
	}
}

vector<float> readRegion(SessionInfo* session, const ReadRegionRequest& req)
{
	if (!session || !session->imageFile.file)
	{
		fmt::print("No file loaded or invalid session\n");
		return vector<float>();
	}

	if (session->currentBand != req.band)
	{
		if (!loadBand(session, req.band))
		{
			fmt::print("Session {}: Select band {} is invalid!\n", session->uuid, req.band);
			return vector<float>();
		}
	}


	if (session->imageFile.height < req.y + req.h || session->imageFile.width < req.x + req.w)
	{
		fmt::print("Session {}: Selected region ({}, {}) -> ({}, {} in band {} is invalid!\n", session->uuid, req.x, req.y, req.x + req.w, req.x + req.h, req.band);
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
					float pixVal = session->currentBandCache[0][req.y + j * req.mip + y][req.x + i * req.mip + x];
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
	auto session = sessions[ws];
	session->eventMutex.lock();
	ReadRegionRequest request;

	if (parseRegionQuery(session, message, request))
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		bool compressed = request.compression >= 4 && request.compression < 32;
		vector<float> regionData = readRegion(session, request);
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

			if (session->currentBandHistogram)
			{
				Value hist(kObjectType);
				hist.AddMember("firstBinCenter", session->currentBandHistogram->firstBinCenter, a);
				hist.AddMember("binWidth", session->currentBandHistogram->binWidth, a);
				hist.AddMember("N", session->currentBandHistogram->N, a);

				Value binsValue(kArrayType);
				binsValue.Reserve(session->currentBandHistogram->N, a);
				for (auto& v: session->currentBandHistogram->bins)
					binsValue.PushBack(v, a);

				hist.AddMember("bins", binsValue, a);
				responseMessage.AddMember("hist", hist, a);
			}
			d.AddMember("message", responseMessage, a);


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
				session->eventMutex.unlock();
				sendEventBinaryPayload(ws, d, binaryPayload, payloadSize);
				delete[] compressionBuffer;
				delete[] binaryPayload;
				auto dtCompress = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

				fmt::print("Session {}: Compressed binary ({:.3f} MB) sent in in {} ms\n", sessions[ws]->uuid, compressedSize / 1e6, dtCompress);
			}
			else
			{
				session->eventMutex.unlock();
				sendEventBinaryPayload(ws, d, regionData.data(), numRows * rowLength * sizeof(float));
				tEnd = std::chrono::high_resolution_clock::now();
				auto dtSent = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
				fmt::print("Session {}, Uncompressed binary ({:.3f} MB) sent in in {} ms\n", sessions[ws]->uuid, numRows * rowLength * sizeof(float) / 1e6, dtSent);

			}
			return;
		}
		else
		{
			fmt::print("Session {}: ReadRegion request is out of bounds\n", sessions[ws]->uuid);
		}
	}
	fmt::print("Session {}: Event is not a valid ReadRegion request !\n", sessions[ws]->uuid);

	Document d;
	Pointer("/event").Set(d, "region_read");
	Pointer("/message/success").Set(d, false);
	session->eventMutex.unlock();
	sendEvent(ws, d);
}

void onFileLoad(uWS::WebSocket<uWS::SERVER>* ws, const Value& message)
{
	auto session = sessions[ws];
	if (!session)
	{
		fmt::print("Missing session!\n");
	}
	session->eventMutex.lock();
	if (message.HasMember("filename") && message["filename"].IsString())
	{
		string filename = message["filename"].GetString();
		if (loadFile(session, fmt::format("{}/{}", baseFolder, filename)))
		{
			fmt::print("Session {}: File {} loaded successfully\n", sessions[ws]->uuid, filename);
			Document d;
			Pointer("/message/numBands").Set(d, session->imageFile.numBands);
			Pointer("/message/success").Set(d, true);
			Pointer("/event").Set(d, "fileload");
			session->eventMutex.unlock();
			sendEvent(ws, d);


			// Profile Z-Profile reads
			vector<float> zProfile;

			vector<float> readTimes;
			srand(time(NULL));
			for (auto i = 0; i < 10; i++)
			{
				auto tStart = std::chrono::high_resolution_clock::now();
				int randX = ((float) rand()) / RAND_MAX * session->imageFile.width;
				int randY = ((float) rand()) / RAND_MAX * session->imageFile.height;
				zProfile = getZProfile(session, randX, randY);
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
				sumX += dt;
				sumX2 += dt * dt;
				minVal = min(minVal, dt);
				maxVal = max(maxVal, dt);
			}

			float mean = sumX / readTimes.size();
			float sigma = sqrt(sumX2 / readTimes.size() - mean * mean);
			fmt::print("Session {} Z Profile reads: N={}; mean={} ms; sigma={} ms; Range: {} -> {} ms\n", sessions[ws]->uuid, readTimes.size(), mean, sigma, minVal, maxVal);


			// Profile Band reads
			vector<float> readTimesBand;
			for (auto i = 0; i < 10; i++)
			{
				auto tStart = std::chrono::high_resolution_clock::now();
				int randZ = ((float) rand()) / RAND_MAX * session->imageFile.numBands;
				loadBand(session, randZ);
				auto tEnd = std::chrono::high_resolution_clock::now();
				auto dtBand = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
				readTimesBand.push_back(dtBand);
			}

			sumX = 0;
			sumX2 = 0;
			minVal = readTimesBand[0];
			maxVal = readTimesBand[0];

			for (auto& dt: readTimesBand)
			{
				sumX += dt;
				sumX2 += dt * dt;
				minVal = min(minVal, dt);
				maxVal = max(maxVal, dt);
			}

			mean = sumX / readTimesBand.size();
			sigma = sqrt(sumX2 / readTimesBand.size() - mean * mean);
			fmt::print("Session {} Band reads: N={}; mean={} ms; sigma={} ms; Range: {} -> {} ms\n", sessions[ws]->uuid, readTimesBand.size(), mean, sigma, minVal, maxVal);
			return;
		}
		else
		{
			fmt::print("Session {}: Error loading file {}\n", sessions[ws]->uuid, filename);
		}
	}

	Document d;
	Pointer("/event").Set(d, "fileload");
	Pointer("/message/success").Set(d, false);
	session->eventMutex.unlock();
	sendEvent(ws, d);
}

void onConnect(uWS::WebSocket<uWS::SERVER>* ws, uWS::HttpRequest httpRequest)
{
	sessions[ws] = new SessionInfo();
	fmt::print("Client {} Connected. Clients: {}\n", sessions[ws]->uuid, sessions.size());
}

void onDisconnect(uWS::WebSocket<uWS::SERVER>* ws, int code, char *message, size_t length)
{
	string s = message;
	s.resize(length);
	auto uuid = sessions[ws]->uuid;
	auto session = sessions[ws];
	if (session)
	{
		delete session->imageFile.file;
		delete session->currentBandHistogram;
		delete session;
		sessions.erase(ws);
	}
	fmt::print("Client {} Disconnected. Remaining clients: {}\n", uuid, sessions.size());
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
};

int main()
{
	uWS::Hub h;

	h.onMessage(&onMessage);
	h.onConnection(&onConnect);
	h.onDisconnection(&onDisconnect);
	if (h.listen(3002))
	{
		h.run();
	}

}