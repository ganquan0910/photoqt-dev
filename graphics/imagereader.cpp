#include "imagereader.h"
#include <QtDebug>

ImageReader::ImageReader(bool v) : QObject() {

	verbose = v;

	gmfiles = "";
	qtfiles = "";
	extrasfiles = "";

}

QImage ImageReader::readImage(QString filename, int rotation, bool zoomed, bool fitinwindow, QSize maxSize, bool dontscale) {

	if(verbose) std::clog << "[reader] zoomed: " << zoomed << std::endl;

	// Which GraphicsEngine should we use?
	QString whatToUse = whatDoIUse(filename);

	if(verbose)
		std::clog << "Using Graphicsengine: "
			  << (whatToUse=="gm" ? "GraphicsMagick" : (whatToUse=="qt" ? "ImageReader" : "External Tool"))
			  << std::endl;

	// Try to use XCFtools for XCF (if enabled)
	if(QFileInfo(filename).suffix().toLower() == "xcf" && whatToUse == "extra")
			return readImage_XCF(filename, rotation, zoomed, fitinwindow, maxSize, dontscale);

	// Try to use GraphicsMagick (if available)
	else if(whatToUse == "gm")
		return readImage_GM(filename, rotation, zoomed, fitinwindow, maxSize, dontscale);

	// Try to use Qt
	return readImage_QT(filename, rotation, zoomed, fitinwindow, maxSize, dontscale);

}

QImage ImageReader::readImage_QT(QString filename, int rotation, bool zoomed, bool fitinwindow, QSize maxSize, bool dontscale) {

	// For reading SVG files
	QSvgRenderer svg;
	QPixmap svg_pixmap;

	// For all other supported file types
	QImageReader reader;

	// Return image
	QImage img;

	// Suffix, for easier access later-on
	QString suffix = QFileInfo(filename).suffix().toLower();

	if(suffix == "svg") {

		// Loading SVG file
		svg.load(filename);

		// Invalid vector graphic
		if(!svg.isValid()) {
			std::cerr << "[reader svg] Error: invalid svg file" << std::endl;
			QPixmap pix(":/img/plainerrorimg.png");
			QPainter paint(&pix);
			QTextDocument txt;
			txt.setHtml("<center><div style=\"text-align: center; font-size: 12pt; font-wight: bold; color: white; background: none;\">ERROR LOADING IMAGE<br><br><bR>The file doesn't contain valid a vector graphic</div></center>");
			paint.translate(100,150);
			txt.setTextWidth(440);
			txt.drawContents(&paint);
			paint.end();
			fileformat = "";
			origSize = pix.size();
			scaleImg1 = -1;
			scaleImg2 = -1;
			animatedImg = false;
			return pix.toImage();
		}

		// Render SVG into pixmap
		svg_pixmap = QPixmap(svg.defaultSize());
		svg_pixmap.fill(Qt::transparent);
		QPainter painter(&svg_pixmap);
		svg.render(&painter);

		// Store the width/height for later use
		origSize = svg.defaultSize();
		// Store the fileformat for later use
		fileformat = "SVG";

	} else {

		// Setting QImageReader
		reader.setFileName(filename);

		// Store the width/height for later use
		origSize = reader.size();
		// Store the fileformat for later use
		if(QFileInfo(filename).baseName() != "photoqt_tmp") fileformat = reader.format().toLower();
		else fileformat = "";

		// Sometimes the size returned by reader.size() is <= 0 (observed for, e.g., .jp2 files)
		// -> then we need to load the actual image to get dimensions
		if(origSize.width() <= 0 || origSize.height() <= 0) {
			QImageReader r;
			r.setFileName(filename);
			origSize = r.read().size();
		}

	}

	// The image width/height
	unsigned int readerWidth = origSize.width();
	unsigned int readerHeight = origSize.height();

	int dispWidth = readerWidth;
	int dispHeight = readerHeight;

	// If the image is rotated to the left or right, the image dimensions are swapped
	if(rotation == 90 || rotation == 270) {
		int tmp = dispWidth;
		dispWidth = dispHeight;
		dispHeight = tmp;
	}


	// Calculate the factor by which the main image (view) has to be zoomed
	float q = 1;

	if(dispWidth > maxSize.width() || (dispWidth != maxSize.width() && fitinwindow)) {
			q = maxSize.width()/(dispWidth*1.0);
			dispWidth *= q;
			dispHeight *= q;
	}

	if(zoomed)
		scaleImg1 = q;
	else
		scaleImg1 = -1;

	// If thumbnails are kept visible, then we need to subtract their height from the absolute height otherwise they overlap with the main image
	if(dispHeight > maxSize.height()) {
		q = maxSize.height()/(dispHeight*1.0);
		dispWidth *= q;
		dispHeight *= q;
	}

	if(zoomed && dispWidth < maxSize.width())
		scaleImg2 = q;
	else
		scaleImg2 = -1;



	// If image is rotated left or right, then we set the right image dimensions again
	if(rotation == 90 || rotation == 270) {
		int tmp = dispHeight;
		dispHeight = dispWidth;
		dispWidth = tmp;
	}


	animatedImg = false;

	// Finalise SVG files
	if(suffix == "svg") {

		// Convert pixmap to image
		img = svg_pixmap.toImage();

		// And scale image
		if(!zoomed)
			img = img.scaled(dispWidth,dispHeight);

	} else {

		// Scale imagereader
		if(!zoomed)
			reader.setScaledSize(QSize(dispWidth,dispHeight));

		// Eventually load the image
		img = reader.read();

		// If an error occured
		if(img.isNull()) {
			QString err = reader.errorString();
			std::cerr << "[reader qt] Error: file failed to load: " << err.toStdString() << std::endl;
			QPixmap pix(":/img/plainerrorimg.png");
			QPainter paint(&pix);
			QTextDocument txt;
			txt.setHtml(QString("<center><div style=\"text-align: center; font-size: 12pt; font-wight: bold; color: white; background: none;\"><b>ERROR LOADING IMAGE</b><br><br><bR>%1</div></center>").arg(err));
			paint.translate(100,150);
			txt.setTextWidth(440);
			txt.drawContents(&paint);
			paint.end();
			fileformat = "";
			origSize = pix.size();
			scaleImg1 = -1;
			scaleImg2 = -1;
			animatedImg = false;
			return pix.toImage();
		}

		if(verbose) std::clog << "[read] image: " << img.width() << " - " << img.height() << " - z: " << zoomed << std::endl;

		// Check for animation support
		if(reader.supportsAnimation() && reader.imageCount() > 1)
			animatedImg = true;

	}

	return img;

}

// If GraphicsMagick supports the file format,
QImage ImageReader::readImage_GM(QString filename, int rotation, bool zoomed, bool fitinwindow, QSize maxSize, bool dontscale) {

#ifdef GM

	QFile file(filename);
	file.open(QIODevice::ReadOnly);
	char *data = new char[file.size()];
	qint64 s = file.read(data, file.size());
	if (s < file.size()) {
		delete[] data;
		if(verbose) std::cerr << "[reader gm] ERROR reading image file data" << std::endl;
		return QImage();
	}

	Magick::Blob blob(data, file.size());
	try {
		Magick::Image image;

		QString suf = QFileInfo(filename).suffix().toLower();

		if(suf == "x" || suf == "avs")

			image.magick("AVS");

		else if(suf == "cals" || suf == "cal" || suf == "dcl"  || suf == "ras")

			image.magick("CALS");

		else if(suf == "cgm")

			image.magick("CGM");

		else if(suf == "cut")

			image.magick("CUT");

		else if(suf == "cur")

			image.magick("CUR");

		else if(suf == "acr" || suf == "dcm" || suf == "dicom" || suf == "dic")

			image.magick("DCM");

		else if(suf == "fax")

			image.magick("FAX");

		else if(suf == "ico")

			image.magick("ICO");

		else if(suf == "mono") {

			image.magick("MONO");
			image.size(Magick::Geometry(4000,3000));

		} else if(suf == "mtv")

			image.magick("MTV");

		else if(suf == "otb")

			image.magick("OTB");

		else if(suf == "palm")

			image.magick("PALM");

		else if(suf == "pfb")

			image.magick("PFB");

		else if(suf == "pict" || suf == "pct" || suf == "pic")

			image.magick("PICT");

		else if(suf == "pix"
			|| suf == "pal")

			image.magick("PIX");

		else if(suf == "tga")

			image.magick("TGA");

		else if(suf == "ttf")

			image.magick("TTF");

		else if(suf == "txt")

			image.magick("TXT");

		else if(suf == "wbm"
			|| suf == "wbmp")

			image.magick("WBMP");


		image.read(blob);
		Magick::Blob ob;
		image.type(Magick::TrueColorMatteType);
		image.magick("PNG");
		image.write(&ob);

		QFile out(QDir::tempPath() + "/photoqt_tmp.png");
		out.open(QIODevice::WriteOnly);
		out.write(static_cast<const char*>(ob.data()), ob.length());

		return readImage_QT(QDir::tempPath() + "/photoqt_tmp.png",rotation,zoomed,fitinwindow,maxSize,dontscale);

	} catch(Magick::Exception &error_) {
		delete[] data;
		std::cerr << "[reader gm] Error: " << error_.what() << std::endl;
		QPixmap pix(":/img/plainerrorimg.png");
		QPainter paint(&pix);
		QTextDocument txt;
		txt.setHtml("<center><div style=\"text-align: center; font-size: 12pt; font-wight: bold; color: white; background: none;\">ERROR LOADING IMAGE<br><br><bR>" + QString(error_.what()) + "</div></center>");
		paint.translate(100,150);
		txt.setTextWidth(440);
		txt.drawContents(&paint);
		paint.end();
		pix.save(QDir::tempPath() + "/photoqt_tmp.png");
		fileformat = "";
		origSize = pix.size();
		scaleImg1 = -1;
		scaleImg2 = -1;
		animatedImg = false;
		return pix.toImage();
	}

#endif

	return QImage();

}

QImage ImageReader::readImage_XCF(QString filename, int rotation, bool zoomed, bool fitinwindow, QSize maxSize, bool dontscale) {

	// We first check if xcftools is actually installed
	QProcess which;
#if QT_VERSION >= 0x050200
	which.setStandardOutputFile(QProcess::nullDevice());
#endif
	which.start("which xcf2png");
	which.waitForFinished();
	// If it isn't -> display error
	if(which.exitCode()) {
		std::cerr << "[reader xcf] Error: xcftools not found" << std::endl;
		QPixmap pix(":/img/plainerrorimg.png");
		QPainter paint(&pix);
		QTextDocument txt;
		txt.setHtml("<center><div style=\"text-align: center; font-size: 12pt; font-wight: bold; color: white; background: none;\">ERROR LOADING IMAGE<br><br><bR>PhotoQt relies on 'xcftools'' to display XCF images, but it wasn't found!</div></center>");
		paint.translate(100,150);
		txt.setTextWidth(440);
		txt.drawContents(&paint);
		paint.end();
		fileformat = "";
		origSize = pix.size();
		scaleImg1 = -1;
		scaleImg2 = -1;
		animatedImg = false;
		return pix.toImage();
	}

	// Convert xcf to png using xcf2png (part of xcftools)
	QProcess p;
	p.execute(QString("xcf2png \"%1\" -o %2").arg(filename).arg(QDir::tempPath() + "/photoqt_tmp.png"));

	// And load it
	return readImage_QT(QDir::tempPath() + "/photoqt_tmp.png",rotation,zoomed,fitinwindow,maxSize,dontscale);

}


bool ImageReader::doIUseMagick(QString filename) {

#ifdef GM
	QStringList qtFiles = qtfiles.split(",");
	QStringList extrasFiles = extrasfiles.split(",");

	for(int i = 0; i < qtFiles.length(); ++i) {
		// We need to remove the first character of qtfiles.at(i), since that is a "*"
		if(filename.toLower().endsWith(QString(qtFiles.at(i)).remove(0,1)))
			return false;
	}
	for(int i = 0; i < extrasFiles.length(); ++i) {
		// We need to remove the first character of qtfiles.at(i), since that is a "*"
		if(filename.toLower().endsWith(QString(extrasFiles.at(i)).remove(0,2)))
			return false;
	}

	return true;
#endif
	return false;

}

QString ImageReader::whatDoIUse(QString filename) {

	QString use = "qt";

	// We need this list for GM and EXTRA below
	QStringList extrasFiles = extrasfiles.split(",");

	// Check for extra
	for(int i = 0; i < extrasFiles.length(); ++i) {
		// We need to remove the first character of qtfiles.at(i), since that is a "*"
		if(filename.toLower().endsWith(QString(extrasFiles.at(i)).remove(0,2)))  {
			use = "extra";
			break;
		}
	}

#ifdef GM

	// Check for GM (i.e., check for not qt and not extra)
	bool usegm = true;
	QStringList qtFiles = qtfiles.split(",");

	for(int i = 0; i < qtFiles.length(); ++i) {
		// We need to remove the first character of qtfiles.at(i), since that is a "*"
		if(filename.toLower().endsWith(QString(qtFiles.at(i)).remove(0,1)))
			usegm = false;
	}
	for(int i = 0; i < extrasFiles.length(); ++i) {
		// We need to remove the first character of qtfiles.at(i), since that is a "*"
		if(filename.toLower().endsWith(QString(extrasFiles.at(i)).remove(0,2)))
			usegm = false;
	}

	if(usegm) use = "gm";
#endif

	return use;

}
