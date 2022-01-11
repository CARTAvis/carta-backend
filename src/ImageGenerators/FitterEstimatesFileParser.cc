#include "FitterEstimatesFileParser.h"

#include <casa/aips.h>
#include <casa/IO/RegularFileIO.h>
#include <casa/Utilities/Regex.h>
#include <casa/Containers/Record.h>
#include <components/ComponentModels/ConstantSpectrum.h>
#include <components/ComponentModels/Flux.h>
#include <components/ComponentModels/GaussianShape.h>
#include <coordinates/Coordinates/CoordinateUtil.h>
#include <coordinates/Coordinates/DirectionCoordinate.h>
#include <images/Images/ImageStatistics.h>


using namespace casacore;
using namespace casa;

FitterEstimatesFileParser::~FitterEstimatesFileParser() {}

ComponentList FitterEstimatesFileParser::getEstimates() const {
	return _componentList;
}

Vector<String> FitterEstimatesFileParser::getFixed() const {
	return _fixedValues;
}

String FitterEstimatesFileParser::getContents() const {
	return _contents;
}

void FitterEstimatesFileParser::_parseFile(
	const casacore::String& estimates
) {
	_contents = estimates;

	Vector<String> lines = stringToVector(_contents, '\n');
	Regex blankLine("^[ \n\t\r\v\f]+$",1000);
	uInt componentIndex = 0;
	Vector<String>::iterator end = lines.end();
	for(Vector<String>::iterator iter=lines.begin(); iter!=end; iter++) {
		if (iter->empty() || iter->firstchar() == '#' ||  iter->matches(blankLine)) {
			// ignore comments and blank lines
			continue;
		}
		uInt commaCount = iter->freq(',');
		ThrowIf(
			commaCount < 5 || commaCount > 6,
			"bad format for line " + *iter
		);
		Vector<String> parts = stringToVector(*iter);
		for (Vector<String>::iterator viter = parts.begin(); viter != parts.end(); viter++) {
			viter->trim();
		}
		String peak = parts[0];
		String xpos = parts[1];
		String ypos = parts[2];
		String maj = parts[3];
		String min = parts[4];
		String pa = parts[5];

		String fixedMask;
		_peakValues.resize(componentIndex + 1, true);
		_xposValues.resize(componentIndex + 1, true);
		_yposValues.resize(componentIndex + 1, true);
		_majValues.resize(componentIndex + 1, true);
		_minValues.resize(componentIndex + 1, true);
		_paValues.resize(componentIndex + 1, true);
		_fixedValues.resize(componentIndex + 1, true);

		ThrowIf(
			! peak.matches(RXdouble),
			"Line " + *iter
				+ ": peak value " + peak + " is not numeric"
		);
		_peakValues(componentIndex) = String::toDouble(peak);

		if (! xpos.matches(RXdouble) ) {
			*_log << "Line " << *iter
				<< ": x position value " << xpos << " is not numeric"
				<< LogIO::EXCEPTION;
		}
		_xposValues(componentIndex) = String::toDouble(xpos);

		if (! ypos.matches(RXdouble) ) {
			*_log << "Line " << *iter
				<< ": y position value " << ypos << " is not numeric"
				<< LogIO::EXCEPTION;
		}
		_yposValues(componentIndex) = String::toDouble(ypos);

		Quantity majQuantity;
		ThrowIf(
			! readQuantity(majQuantity, maj),
			"Line " + *iter
				+ ": Major axis value " + maj + " is not a quantity"
		);
		_majValues(componentIndex) = majQuantity;

		Quantity minQuantity;
		ThrowIf(
			! readQuantity(minQuantity, min),
			"Line " + *iter
				+ ": Major axis value " + min + " is not a quantity"
		);
		_minValues(componentIndex) = minQuantity;

		Quantity paQuantity;
		ThrowIf(
			! readQuantity(paQuantity, pa),
			"Line " + *iter
				+ ": Position angle value " + pa + " is not a quantity"
		);
		_paValues(componentIndex) = paQuantity;

		if (parts.size() == 7) {
			fixedMask = parts[6];
			for (
				String::iterator siter = fixedMask.begin();
				siter != fixedMask.end(); siter++
			) {
				if (
					*siter != 'a' && *siter != 'b' && *siter != 'f'
					&& *siter != 'p' && *siter != 'x' && *siter != 'y'
				) {
					*_log << "fixed parameter ID " << String(*siter) << " is not recognized"
						<< LogIO::EXCEPTION;
				}
			}
			_fixedValues(componentIndex) = fixedMask;
		}
		_fixedValues(componentIndex) = fixedMask;
		componentIndex++;
	}
	ThrowIf(componentIndex == 0, "No valid estmates were found");
}
