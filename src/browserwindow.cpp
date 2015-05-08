/*  SMTUBE2, a small YouTube browser for SMPlayer
    Copyright (C) 2015 Ricardo Villalba <rvm@users.sourceforge.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "browserwindow.h"

#include <QTextEdit>
#include <QLineEdit>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QNetworkProxyFactory>
#include <QProcess>
#include <QSettings>
#include <QMessageBox>

#include "mywebview.h"
#include "mywebpage.h"
#include "mycookiejar.h"

#include "supportedurls.h"
#include "retrieveyoutubeurl.h"

#include "configdialog.h"
#include "version.h"

#ifdef YT_USE_SCRIPT
#include "ytsig.h"
#endif


BrowserWindow::BrowserWindow(const QString & config_path, QWidget * parent, Qt::WindowFlags flags)
	: QMainWindow(parent, flags)
#ifdef USE_PLAYERS
	, current_player(-1)
#endif
{
	setWindowIcon(QPixmap(":/icons/smtube.png"));
	home_page = "http://smtube.org/videos/";

	settings = new QSettings(config_path + "/smtube2.ini", QSettings::IniFormat, this);

	ryu = new RetrieveYoutubeUrl(this);
	connect(ryu, SIGNAL(gotPreferredUrl(const QString &)), this, SLOT(openYTUrl(const QString &)));

	QNetworkProxyFactory::setUseSystemConfiguration(true);

	MyWebPage * page = new MyWebPage(this);

	view = new MyWebView(this);
	connect(view, SIGNAL(requestedOpenWith(const QString&, const QUrl&)), this, SLOT(openWith(const QString&, const QUrl&)));

	view->setPage(page);
	view->page()->setLinkDelegationPolicy(QWebPage::DelegateAllLinks);
	view->page()->settings()->setAttribute(QWebSettings::PluginsEnabled, false);
	view->page()->settings()->setAttribute(QWebSettings::JavaEnabled, false);
	view->page()->settings()->setAttribute(QWebSettings::JavascriptEnabled, true);
	//view->page()->settings()->setAttribute(QWebSettings::AutoLoadImages, false);
	//view->page()->settings()->setAttribute(QWebSettings::DeveloperExtrasEnabled, true);

	MyCookieJar * jar = new MyCookieJar(config_path + "/cookies.ini");
	view->page()->networkAccessManager()->setCookieJar(jar);

	connect(view, SIGNAL(loadFinished(bool)), SLOT(adjustLocation()));
	connect(view, SIGNAL(titleChanged(QString)), SLOT(adjustTitle()));
	connect(view, SIGNAL(loadProgress(int)), SLOT(setProgress(int)));
	connect(view, SIGNAL(loadFinished(bool)), SLOT(finishLoading(bool)));
	connect(view->page(), SIGNAL(linkClicked(const QUrl&)), this, SLOT(processLink(const QUrl&)));
	//connect(view, SIGNAL(statusBarMessage(const QString &)), statusBar(), SLOT(showMessage(const QString &)));
	connect(view->page(), SIGNAL(linkHovered(const QString &, const QString &, const QString &)), 
            statusBar(), SLOT(showMessage(const QString &)));

	location = new QLineEdit(this);
	location->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	connect(location, SIGNAL(returnPressed()), SLOT(changeLocation()));

	QAction * loadHomePageAct = new QAction(QPixmap(":/icons/home.png"), tr("Home"), this);
	connect(loadHomePageAct, SIGNAL(triggered()), SLOT(loadHomePage()));

	toolbar = addToolBar(tr("Navigation"));
	toolbar->addAction(loadHomePageAct);
	toolbar->addAction(view->pageAction(QWebPage::Back));
	toolbar->addAction(view->pageAction(QWebPage::Forward));
	toolbar->addAction(view->pageAction(QWebPage::Reload));
	toolbar->addAction(view->pageAction(QWebPage::Stop));
	toolbar->addWidget(location);

	QMenu * viewMenu = menuBar()->addMenu(tr("&View"));
	toggleToolbarAct = new QAction(tr("Toolbar"), this);
	toggleToolbarAct->setCheckable(true);
	connect(toggleToolbarAct, SIGNAL(toggled(bool)), this, SLOT(viewToolbar(bool)));

	toggleStatusbarAct = new QAction(tr("Statusbar"), this);
	toggleStatusbarAct->setCheckable(true);
	connect(toggleStatusbarAct, SIGNAL(toggled(bool)), this, SLOT(viewStatusbar(bool)));

	QAction * showConfigDialogAct = new QAction(tr("Configuration"), this);
	connect(showConfigDialogAct, SIGNAL(triggered()), this, SLOT(showConfigDialog()));

	viewMenu->addAction(toggleToolbarAct);
	viewMenu->addAction(toggleStatusbarAct);
	viewMenu->addAction(showConfigDialogAct);

	QMenu * helpMenu = menuBar()->addMenu(tr("&Help"));
	QAction * aboutAct = new QAction(tr("About"), this);
	connect(aboutAct, SIGNAL(triggered()), this, SLOT(showAbout()));
	helpMenu->addAction(aboutAct);

	setCentralWidget(view);

	//menuBar()->hide();
	toolbar->hide();
	statusBar()->hide();

	loadConfig();
}

BrowserWindow::~BrowserWindow() {
	saveConfig();
}

void BrowserWindow::viewToolbar(bool b) {
	qDebug() << "BrowserWindow::viewToolbar:" << b;
	toolbar->setVisible(b);
//	toggleToolbarAct->setChecked(b);
}

void BrowserWindow::viewStatusbar(bool b) {
	qDebug() << "BrowserWindow::viewStatusbar:" << b;
	statusBar()->setVisible(b);
//	toggleStatusbarAct->setChecked(b);
}

void BrowserWindow::loadUrl(const QUrl & url) {
	view->load(url);
}

void BrowserWindow::search(const QString & term) {
	QString q = home_page + "search.php?q=" + term;
	loadUrl(QUrl(q));
}

void BrowserWindow::adjustLocation() {
	QString u = view->url().toString();
	qDebug() << "BrowserWindow::adjustLocation:" << u;
	location->setText(u);
}

void BrowserWindow::changeLocation() {
	QString text = location->text();
	qDebug() << "BrowserWindow::changeLocation:" << text;

	QRegExp rx("((https?:)?//)?(([\\d\\w]|%[a-fA-f\\d]{2,2})+(:([\\d\\w]|%[a-fA-f\\d]{2,2})+)?@)?([\\d\\w][-\\d\\w]{0,253}[\\d\\w]\\.)+[\\w]{2,63}(:[\\d]+)?(/([-+_~.\\d\\w]|%[a-fA-f\\d]{2,2})*)*(\\?(&?([-+_~.\\d\\w]|%[a-fA-f\\d]{2,2})=?)*)?(#([-+_~.\\d\\w]|%[a-fA-f\\d]{2,2})*)?");
	bool valid = (rx.indexIn(text) > -1);
	qDebug() << "BrowserWindow::changeLocation: url valid:" << valid;

	if (!valid) {
		search(text);
	} else {
		view->load(QUrl::fromUserInput(text));
	}
	view->setFocus();
}

void BrowserWindow::adjustTitle() {
	setWindowTitle(view->title());
}

void BrowserWindow::setProgress(int p) {
	statusBar()->showMessage(tr("Loading... %1%").arg(p));
}

void BrowserWindow::finishLoading(bool) {
	statusBar()->clearMessage();
}

void BrowserWindow::processLink(const QUrl & url ) {
	qDebug() << "BrowserWindow::processLink:" << url.toString();

	QString link = url.toString();

	bool can_play_this = false;
	int site_type = SupportedUrls::site(url.toString());

	#ifdef USE_PLAYERS
	// Try to play with the first player on the list
	int player_id = 0;
	QList<Player> pl = players.availablePlayers();
	if (pl.count() > 0) {
		if (site_type == SupportedUrls::Youtube) can_play_this = true;
		else
		if (site_type == SupportedUrls::Other && pl[player_id].directPlay()) can_play_this = true;
	}
	#else
	if (site_type == SupportedUrls::Youtube) can_play_this = true;
	else
	if (site_type == SupportedUrls::Other && HCPLAYER_DIRECTPLAY) can_play_this = true;
	#endif

	if (!can_play_this) {
		view->load(url);
	} else {
		#ifdef USE_PLAYERS
		openWith(pl[player_id].name(), QUrl(link));
		#else
		openWith(0, QUrl(link));
		#endif
	}
}

void BrowserWindow::openWith(int player_id, const QUrl & url) {
	qDebug() << "BrowserWindow::openWith: player_id:" << player_id << "url:" << url.toString();

#ifdef USE_PLAYERS
	QString binary = players.item(player_id).executable();
	QString player_name = players.item(player_id).name();
	QString arguments = players.item(player_id).arguments();
	bool direct_play = players.item(player_id).directPlay();
#else
	QString binary = HCPLAYER_EXECUTABLE;
	QString player_name = HCPLAYER_NAME;
	QString arguments = HCPLAYER_ARGUMENTS;
	bool direct_play = HCPLAYER_DIRECTPLAY;
#endif

	if (direct_play) {
		qDebug() << "BrowserWindow::openWith: ready to play with" << player_name;
		if (binary.contains(" ")) binary = "\""+ binary +"\"";
		QString command = binary +" "+ QString(arguments).replace("%u", "\""+ url.toString() +"\"").replace("%t", "\" \"");
		qDebug() << "BrowserWindow::openWith: command:" << command;
		QProcess::startDetached(binary, QStringList() << url.toString());
	} else {
		qDebug() << "BrowserWindow::openWith:" << player_name << "can't play this URL";
		#ifdef USE_PLAYERS
		current_player = player_id;
		#endif
		#ifdef YT_USE_SCRIPT
		YTSig::setScriptFile(script_file);
		#endif
		ryu->fetchPage(url.toString());
	}
}

void BrowserWindow::openWith(const QString & player, const QUrl & url) {
	qDebug() << "BrowserWindow::openWith: player:" << player << "url:" << url.toString();

#ifdef USE_PLAYERS
	int p = players.findName(player);
	if (p != -1) {
		openWith(p, url);
	} else {
		qWarning() << "BrowserWindow::openWith: player not found";
	}
#else
	openWith(0, url);
#endif
}

void BrowserWindow::openYTUrl(const QString & url) {
	qDebug() << "BrowserWindow::openYTUrl:" << url;

#ifdef USE_PLAYERS
	if (current_player == -1) {
		qDebug() << "BrowserWindow::openYTUrl: invalid player";
		return;
	}
#endif

	QString title = ryu->urlTitle();
	int itag = ryu->itagFromPreferredQuality();
	qDebug() << "BrowserWindow::openYTUrl: itag:" << itag;

	QString filename = title.replace(" - YouTube", "");
	filename += RetrieveYoutubeUrl::extensionForItag(itag);

	qDebug() << "BrowserWindow::openYTUrl: filename:" << filename;

#ifdef USE_PLAYERS
	QString binary = players.item(current_player).executable();
	QString arguments = players.item(current_player).arguments();
#else
	QString binary = HCPLAYER_EXECUTABLE;
	QString arguments = HCPLAYER_ARGUMENTS;
#endif

	if (binary.contains(" ")) binary = "\""+ binary +"\"";

	QString expanded_arguments = QString(arguments).replace("%u", "\""+ url +"\"");
	if (!url.contains("%f") && !url.contains("%t")) {
		expanded_arguments = expanded_arguments.replace("%f", "\"" + filename +"\"")
                                               .replace("%t", "\"" + title +"\"");
	}

	QString command = binary + " " + expanded_arguments;
	qDebug() << "BrowserWindow::openYTUrl: command:" << command;

	QProcess::startDetached(command);
}

void BrowserWindow::showAbout() {
	QMessageBox::about(this, tr("About SMTube"),
		tr("This is an early version of the new SMTube.") +"<br>"+
		tr("Many things are still missing.") +
		"<p>&copy; 2015 Ricardo Villalba &lt;rvm@users.sourceforge.net&gt;"
		"<p>"+
		tr("License: %1").arg("GPL v.2") +
		"<p>"+
		tr("Version: %1").arg(smtubeVersion()));
}

void BrowserWindow::showConfigDialog() {
	ConfigDialog d(this);
	d.setPlayers(players.allPlayers());

	if (d.exec() == QDialog::Accepted) {
		players.setAllPlayers(d.players());
		view->setPlayers(players.availablePlayers());
	}
}

void BrowserWindow::saveConfig() {
	qDebug() << "BrowserWindow::saveConfig";

	settings->beginGroup("window");
	settings->setValue("size", size());
	settings->setValue("pos", pos());
	settings->endGroup();

	settings->beginGroup("view");
	settings->setValue("toolbar", toggleToolbarAct->isChecked());
	settings->setValue("statusbar", toggleStatusbarAct->isChecked());
	settings->endGroup();

	settings->beginGroup("general");
	settings->setValue("playback_quality", ryu->preferredQuality());
	settings->endGroup();

#ifdef USE_PLAYERS
	players.save(settings);
#endif
}

void BrowserWindow::loadConfig() {
	qDebug() << "BrowserWindow::loadConfig";

	settings->beginGroup("window");
	resize(settings->value("size", QSize(796, 711)).toSize());
	move(settings->value("pos", pos()).toPoint());
	settings->endGroup();

	settings->beginGroup("view");
	toggleToolbarAct->setChecked(settings->value("toolbar", false).toBool());
	toggleStatusbarAct->setChecked(settings->value("statusbar", true).toBool());
	settings->endGroup();

	settings->beginGroup("general");
	int quality = settings->value("playback_quality", RetrieveYoutubeUrl::MP4_360p).toInt();
	ryu->setPreferredQuality((RetrieveYoutubeUrl::Quality) quality);
	settings->endGroup();

	SupportedUrls::load();

#ifdef USE_PLAYERS
	players.load(settings);
	view->setPlayers(players.availablePlayers());
#else
	view->setPlayer(HCPLAYER_NAME);
#endif
}

#include "moc_browserwindow.cpp"