#ifndef IMAGEANALYSIS_FITTERESTIMATESFILEPARSER_H
#define IMAGEANALYSIS_FITTERESTIMATESFILEPARSER_H

#include <casa/aips.h>
#include <casa/OS/RegularFile.h>
#include <components/ComponentModels/ComponentList.h>
#include <images/Images/ImageInterface.h>
#include <memory>

using namespace casa;

class FitterEstimatesFileParser {
	public:

    FitterEstimatesFileParser() = delete;

    template <class T> explicit FitterEstimatesFileParser(
        const casacore::String& estimates,
        const casacore::ImageInterface<T>& image
    );

		~FitterEstimatesFileParser();

		// Get the estimates specified in the file as a ComponentList object.
		ComponentList getEstimates() const;

		// Get the fixed parameter masks specified in the file.
		casacore::Vector<casacore::String> getFixed() const;

		// Get the contents of the file
		casacore::String getContents() const;

	private:
		ComponentList _componentList;
		casacore::Vector<casacore::String> _fixedValues;
		std::unique_ptr<casacore::LogIO> _log;
		casacore::Vector<casacore::Double> _peakValues, _xposValues, _yposValues;
		casacore::Vector<casacore::Quantity> _majValues, _minValues, _paValues;
		casacore::String _contents;

		// parse the file
		void _parseFile(const casacore::String& estimates);
		template <class T> void _createComponentList(
		    const casacore::ImageInterface<T>& image
		);
};

#ifndef AIPS_NO_TEMPLATE_SRC
#include "FitterEstimatesFileParser2.tcc"
#include "FitterEstimatesFileParser.cc"
#endif

#endif
