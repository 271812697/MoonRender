#include "editor/UI/HomePanel/RecentProjects.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include "core/log.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "Core/Global/ServiceLocator.h"

namespace
{
	constexpr const char* kSettingsGroup = "recentProjects";
	/** The size of the picture of a project: a card is wider than tall and the
	 * pictures are cut to that shape when they are made. */
	constexpr int kPreviewWidth = 480;
	constexpr int kPreviewHeight = 270;

	QString previewFolder()
	{
		const QString root
			= QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		const QString folder = root + QStringLiteral("/project_previews");
		QDir().mkpath(folder);
		return folder;
	}
}

namespace MOON
{
	QString RecentProjects::PreviewFilePath(const QString& p_path)
	{
		// The name is a hash of the path: a project keeps its picture when it is moved
		// back to where it was, and two projects in different folders cannot take each
		// other's.
		const QByteArray digest = QCryptographicHash::hash(
			p_path.toUtf8(), QCryptographicHash::Sha1).toHex();
		return previewFolder() + QStringLiteral("/") + QString::fromLatin1(digest)
			+ QStringLiteral(".png");
	}

	std::vector<RecentProject> RecentProjects::Load()
	{
		QSettings settings(QStringLiteral("MOON"), QStringLiteral("MoonCAD"));
		std::vector<RecentProject> projects;
		const int count = settings.beginReadArray(kSettingsGroup);
		for (int i = 0; i < count && static_cast<int>(projects.size()) < kMaxCount; ++i) {
			settings.setArrayIndex(i);
			RecentProject project;
			project.path = settings.value(QStringLiteral("path")).toString();
			project.lastOpened = settings.value(QStringLiteral("lastOpened")).toDateTime();
			if (project.path.isEmpty()) {
				continue;
			}
			const QFileInfo info(project.path);
			if (!info.isFile()) {
				// A project that is no longer where it was is not worth a card; it is
				// dropped here rather than shown and failing when it is clicked.
				continue;
			}
			project.name = info.completeBaseName();
			const QString preview = PreviewFilePath(project.path);
			if (QFileInfo::exists(preview)) {
				project.preview.load(preview);
			}
			projects.push_back(std::move(project));
		}
		settings.endArray();
		return projects;
	}

	void RecentProjects::Remember(const QString& p_path, const QImage& p_preview)
	{
		const QFileInfo info(p_path);
		if (!info.isFile()) {
			return;
		}
		std::vector<RecentProject> projects = Load();

		// The entry that is being remembered goes first; the others keep their order.
		RecentProject entry;
		entry.path = info.absoluteFilePath();
		entry.name = info.completeBaseName();
		entry.lastOpened = QDateTime::currentDateTime();
		for (const RecentProject& project : projects) {
			if (QFileInfo(project.path).absoluteFilePath() != entry.path) {
				continue;
			}
			entry.preview = project.preview;
			break;
		}
		if (!p_preview.isNull()) {
			const QImage scaled = p_preview.scaled(
				kPreviewWidth, kPreviewHeight, Qt::KeepAspectRatioByExpanding,
				Qt::SmoothTransformation);
			const QString file = PreviewFilePath(entry.path);
			if (scaled.save(file, "PNG")) {
				entry.preview.load(file);
			}
			else {
				CORE_WARN("[Recent] the preview of {0} could not be written", file.toStdString());
			}
		}

		std::vector<RecentProject> kept;
		kept.push_back(std::move(entry));
		for (const RecentProject& project : projects) {
			if (static_cast<int>(kept.size()) >= kMaxCount) {
				break;
			}
			if (QFileInfo(project.path).absoluteFilePath() == kept.front().path) {
				continue;
			}
			kept.push_back(project);
		}

		QSettings settings(QStringLiteral("MOON"), QStringLiteral("MoonCAD"));
		settings.remove(kSettingsGroup);
		settings.beginWriteArray(kSettingsGroup, static_cast<int>(kept.size()));
		for (int i = 0; i < static_cast<int>(kept.size()); ++i) {
			settings.setArrayIndex(i);
			settings.setValue(QStringLiteral("path"), kept[i].path);
			settings.setValue(QStringLiteral("lastOpened"), kept[i].lastOpened);
		}
		settings.endArray();
	}

	void RecentProjects::Forget(const QString& p_path)
	{
		const QString path = QFileInfo(p_path).absoluteFilePath();
		std::vector<RecentProject> projects = Load();
		std::vector<RecentProject> kept;
		for (const RecentProject& project : projects) {
			if (QFileInfo(project.path).absoluteFilePath() == path) {
				QFile::remove(PreviewFilePath(project.path));
				continue;
			}
			kept.push_back(project);
		}

		QSettings settings(QStringLiteral("MOON"), QStringLiteral("MoonCAD"));
		settings.remove(kSettingsGroup);
		settings.beginWriteArray(kSettingsGroup, static_cast<int>(kept.size()));
		for (int i = 0; i < static_cast<int>(kept.size()); ++i) {
			settings.setArrayIndex(i);
			settings.setValue(QStringLiteral("path"), kept[i].path);
			settings.setValue(QStringLiteral("lastOpened"), kept[i].lastOpened);
		}
		settings.endArray();
	}

	QImage RecentProjects::GrabViewportPreview()
	{
		// The viewport is a QOpenGLWidget: asking it for its frame is what Qt has for
		// this, and it hands back what is on screen without the panels around it.
		const QImage frame = GetViewerWidget.grabFramebuffer();
		if (frame.isNull()) {
			return frame;
		}
		return frame.scaled(
			kPreviewWidth, kPreviewHeight, Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
	}
}
