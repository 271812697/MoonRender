#include "openFile.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "Core/Global/ServiceLocator.h"
#include "core/log.h"
#include "surfacemesh/algorithm/decimation.h"
#include "surfacemesh/read_mesh.h"
#include <fstream>
#include <QtWidgets/QFileDialog>
#include <QCoreApplication>
namespace MOON {

	void write_stl(const SurfaceMesh& mesh, const std::filesystem::path& file
		)
	{
		if (!mesh.is_triangle_mesh())
		{
			auto what = "write_stl: Not a triangle mesh.";
			throw InvalidInputException(what);
		}

		auto fnormals = mesh.get_face_property<Normal>("f:normal");
		if (!fnormals)
		{
			auto what = "write_stl: No face normals present.";
			throw InvalidInputException(what);
		}


		std::ofstream ofs(file.string().c_str());
		auto points = mesh.get_vertex_property<Point>("v:point");

		ofs << "solid stl\n";

		for (const auto& f : mesh.faces())
		{
			const auto& n = fnormals[f];
			ofs << "  facet normal ";
			ofs << n[0] << " " << n[1] << " " << n[2] << "\n";
			ofs << "    outer loop\n";
			for (const auto& v : mesh.vertices(f))
			{
				const auto& p = points[v];
				ofs << "      vertex ";
				ofs << p[0] << " " << p[1] << " " << p[2] << "\n";
			}
			ofs << "    endloop\n";
			ofs << "  endfacet\n";
		}
		ofs << "endsolid\n";
		ofs.close();
	}

	//-----------------------------------------------------------------------------
	OpenFileCommand::OpenFileCommand(QObject* parentObject)
		: Command(parentObject)
	{
		
		auto& viewer = GetService(ViewerWidget);
		
		connect(this, &OpenFileCommand::readFilePath, &viewer, &ViewerWidget::onReadFile);
		auto openfile=new QAction(this);
		setAction(openfile);
		openfile->setObjectName(QString::fromUtf8("actionFileOpen"));
		openfile->setText("&Open");
		openfile->setStatusTip("Open");
		openfile->setShortcut(QCoreApplication::translate("pqFileMenuBuilder", "Ctrl+O", nullptr));
		QIcon icon9;
		icon9.addFile(QString::fromUtf8(":/widgets/icons/pqOpen.svg"), QSize(), QIcon::Normal, QIcon::Off);
		openfile->setIcon(icon9);
	}

	void OpenFileCommand::execute()
	{
		QString fileName = QFileDialog::getOpenFileName(nullptr,
			tr("Open Flow Scene"),
			QDir::homePath(),
			tr("Moon Document (*.moon);;Flow Scene Files (*.scene;*.gltf;*.obj;*.stl);;All Files (*.*)"));
		if (!QFileInfo::exists(fileName))
			return;
		CORE_INFO("Read File {0}", fileName.toStdString());
		emit readFilePath(fileName);
	}
}




