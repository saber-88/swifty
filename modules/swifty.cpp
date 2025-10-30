#include "headers/swifty.h"
#include "headers/kineticscrollarea.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <cmath>

ClickableLabel::ClickableLabel(const QString &path, QWidget *parent)
    : QLabel(parent), imagePath(path) {
  setCursor(Qt::PointingHandCursor);
  setAlignment(Qt::AlignCenter);
}

void ClickableLabel::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton)
    emit clicked(imagePath);
  QLabel::mousePressEvent(event);
}

KineticScrollArea::KineticScrollArea(QWidget *parent)
    : QScrollArea(parent), momentumTimer(new QTimer(this)),
      scalingTimer(new QTimer(this)) {
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setWidgetResizable(true);
  momentumTimer->setInterval(16);
  connect(momentumTimer, &QTimer::timeout, this,
          &KineticScrollArea::handleMomentum);
  scalingTimer->setInterval(16);
  connect(scalingTimer, &QTimer::timeout, this,
          &KineticScrollArea::updateLabelScaling);
  scalingTimer->start();
}

void KineticScrollArea::setLabels(const QList<ClickableLabel *> &newLabels) {
  labels = newLabels;
}

void KineticScrollArea::mousePressEvent(QMouseEvent *event) {
  momentumTimer->stop();
  velocity = 0;
  lastPos = event->pos();
  lastTime.restart();
  QScrollArea::mousePressEvent(event);
}

void KineticScrollArea::mouseMoveEvent(QMouseEvent *event) {
  int deltaX = lastPos.x() - event->pos().x();
  horizontalScrollBar()->setValue(horizontalScrollBar()->value() + deltaX);
  qint64 elapsed = lastTime.elapsed();
  if (elapsed > 0) {
    double instantVelocity = deltaX * 1000.0 / elapsed;
    velocity = velocity * 0.5 + instantVelocity * 0.5;
  }
  lastPos = event->pos();
  lastTime.restart();
  QScrollArea::mouseMoveEvent(event);
}

void KineticScrollArea::mouseReleaseEvent(QMouseEvent *event) {
  if (std::abs(velocity) > 0.01)
    momentumTimer->start();
  QScrollArea::mouseReleaseEvent(event);
}

void KineticScrollArea::handleMomentum() {
  velocity *= 0.92;
  if (std::abs(velocity) < 0.05) {
    momentumTimer->stop();
    velocity = 0;
    return;
  }
  double newValue = horizontalScrollBar()->value() + velocity * 0.016;
  horizontalScrollBar()->setValue(qRound(newValue));
}

void KineticScrollArea::updateLabelScaling() {
  if (labels.isEmpty())
    return;
  double centerX = viewport()->width() / 2 + horizontalScrollBar()->value();
  for (auto label : labels) {
    double labelCenter = label->x() + label->width() / 2;
    double distance = std::abs(labelCenter - centerX);
    double t = std::min(distance / 400.0, 1.0);
    double scaleTarget = 1.0 - 0.5 * std::pow(t, 1.8);
    QPixmap pix = label->pixmap(Qt::ReturnByValue);
    if (pix.isNull())
      continue;
    double currentScale = label->width() / double(pix.width());
    double newScale = currentScale + (scaleTarget - currentScale) * 0.25;
    label->setFixedSize(pix.size() * newScale);
  }
}


Swifty::Swifty(QWidget *parent) : QWidget(parent) {
   QScreen *screen = QGuiApplication::primaryScreen();
  int screenWidth = screen ? screen->geometry().width() : 1920;
  int desiredWidth = static_cast<int>(screenWidth * 0.88); // 88% of 1920 ≈ 1690
  int desiredHeight = 200;

  setFixedSize(desiredWidth, desiredHeight); // Get the primary screen dimensions

   setStyleSheet("background-color:black;border-radius:8px;");
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
  scrollArea = new KineticScrollArea(this);
  scrollArea->setGeometry(rect());
  scrollArea->setStyleSheet("background:black;border:none;");
  containerWidget = new QWidget();
  containerWidget->setStyleSheet("background:black;");
  hLayout = new QHBoxLayout(containerWidget);
  hLayout->setSpacing(8);
  hLayout->setContentsMargins(5, 1, 5, 1);
  scrollArea->setWidget(containerWidget);
  cleanupCache();
  loadWallpapers();
  scrollArea->setLabels(labels);
}

void Swifty::showEvent(QShowEvent *) {
  if (QScreen *screen = QGuiApplication::primaryScreen()) {
    QRect geom = screen->geometry();
    // Use the newly computed width and height (from constructor)
    move((geom.width() - width()) / 2, geom.height() - height() - 5);
  }
}


QString Swifty::swiftyCachePath() const {
  QDir dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
           "/swifty");
  dir.mkpath(".");
  return dir.absolutePath();
}

// === RECURSIVE CLEANUP ===
void Swifty::cleanupCache() {
  QDir cacheDir(swiftyCachePath());
  QStringList cacheFiles = cacheDir.entryList({"*.jpg"}, QDir::Files);

  QDir picturesDir(QDir::homePath() + "/Pictures/Wallpapers"); 
  QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.gif"};

  QSet<QString> validHashes;
  QDirIterator it(picturesDir.absolutePath(), filters, QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString path = it.next();
    validHashes.insert(QString(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1)
            .toHex()));
  }

  for (const QString &thumbFile : cacheFiles) {
    QString hashName = QFileInfo(thumbFile).completeBaseName();
    if (!validHashes.contains(hashName))
      QFile::remove(cacheDir.filePath(thumbFile));
  }
}

// === RECURSIVE WALLPAPER LOADING ===
void Swifty::loadWallpapers() {
// Set to ~/Pictures/Wallpapers
  QDir picturesDir(QDir::homePath() + "/Pictures/Wallpapers"); 
  if (!picturesDir.exists()) {
    picturesDir.mkpath(".");  // Creates the directory and all needed parent directories
}
  QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.gif"};
  QString cachePath = swiftyCachePath();

  QDirIterator it(picturesDir.absolutePath(), filters, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString path = it.next();
    QByteArray hash =
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1)
            .toHex();
    QString thumbPath = cachePath + "/" + hash + ".jpg";

    QPixmap thumbnail;
    if (!QFile::exists(thumbPath)) {
      QImage img(path);
      if (img.isNull())
        continue;
      int h = 180;
      int w = img.width() * h / img.height();
      img = img.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      img.save(thumbPath);
      thumbnail = QPixmap::fromImage(img);
    } else {
      thumbnail.load(thumbPath);
    }

    if (thumbnail.isNull())
      continue;

    ClickableLabel *label = new ClickableLabel(path);
    label->setPixmap(thumbnail);
    label->setFixedSize(thumbnail.size());
    connect(label, &ClickableLabel::clicked, this, &Swifty::applyWallpaper);
    hLayout->addWidget(label);
    labels.append(label);
  }
}

void Swifty::applyWallpaper(const QString &path) {
  QString wallpaperName = QFileInfo(path).completeBaseName();
  QProcess::startDetached("notify-send", QStringList() << "-t" << "1200" << (wallpaperName + " Applied!"));
  QStringList arguments;
  arguments << "img" << path << "--transition-type" << "wipe"
            << "--transition-duration" << "1.5";
  QProcess::startDetached("swww", arguments);

  QString hyprlockDir = "/home/safal726/.cache/hyprlock-safal";
  QDir dir(hyprlockDir);
  if (!dir.exists())
    dir.mkpath(".");
  QString finalPath = hyprlockDir + "/bg.jpg";
  QImage img(path);
  if (!img.isNull())
    img.save(finalPath, "JPG");
 QGuiApplication::exit(0);
}
