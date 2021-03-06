/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "core/observer.h"
#include "storage/localimageloader.h" // for TaskId

namespace Storage {

class Downloader final {
public:
	int currentPriority() const {
		return _priority;
	}
	void clearPriorities();

	base::Observable<void> &taskFinished() {
		return _taskFinishedObservable;
	}

private:
	base::Observable<void> _taskFinishedObservable;
	int _priority = 1;

};

} // namespace Storage

struct StorageImageSaved {
	StorageImageSaved() = default;
	explicit StorageImageSaved(const QByteArray &data) : data(data) {
	}

	QByteArray data;

};

enum LocalLoadStatus {
	LocalNotTried,
	LocalNotFound,
	LocalLoading,
	LocalLoaded,
	LocalFailed,
};

class mtpFileLoader;
class webFileLoader;

struct FileLoaderQueue;
class FileLoader : public QObject {
	Q_OBJECT

public:
	FileLoader(const QString &toFile, int32 size, LocationType locationType, LoadToCacheSetting, LoadFromCloudSetting fromCloud, bool autoLoading);
	bool finished() const {
		return _finished;
	}
	bool cancelled() const {
		return _cancelled;
	}
	const QByteArray &bytes() const {
		return _data;
	}
	virtual uint64 objId() const {
		return 0;
	}
	QByteArray imageFormat(const QSize &shrinkBox = QSize()) const;
	QPixmap imagePixmap(const QSize &shrinkBox = QSize()) const;
	QString fileName() const {
		return _fname;
	}
	float64 currentProgress() const;
	virtual int32 currentOffset(bool includeSkipped = false) const = 0;
	int32 fullSize() const;

	bool setFileName(const QString &filename); // set filename for loaders to cache
	void permitLoadFromCloud();

	void pause();
	void start(bool loadFirst = false, bool prior = true);
	void cancel();

	bool loading() const {
		return _inQueue;
	}
	bool paused() const {
		return _paused;
	}
	bool started() const {
		return _inQueue || _paused;
	}
	bool loadingLocal() const {
		return (_localStatus == LocalLoading);
	}
	bool autoLoading() const {
		return _autoLoading;
	}

	virtual void stop() {
	}
	virtual ~FileLoader();

	void localLoaded(const StorageImageSaved &result, const QByteArray &imageFormat = QByteArray(), const QPixmap &imagePixmap = QPixmap());

signals:
	void progress(FileLoader *loader);
	void failed(FileLoader *loader, bool started);

protected:
	void readImage(const QSize &shrinkBox) const;

	Storage::Downloader *_downloader = nullptr;
	FileLoader *_prev = nullptr;
	FileLoader *_next = nullptr;
	int _priority = 0;
	FileLoaderQueue *_queue;

	bool _paused = false;
	bool _autoLoading = false;
	bool _inQueue = false;
	bool _finished = false;
	bool _cancelled = false;
	mutable LocalLoadStatus _localStatus = LocalNotTried;

	virtual bool tryLoadLocal() = 0;
	virtual void cancelRequests() = 0;

	void startLoading(bool loadFirst, bool prior);
	void removeFromQueue();
	void cancel(bool failed);

	void loadNext();
	virtual bool loadPart() = 0;

	QFile _file;
	QString _fname;
	bool _fileIsOpen = false;

	LoadToCacheSetting _toCache;
	LoadFromCloudSetting _fromCloud;

	QByteArray _data;

	int32 _size;
	LocationType _locationType;

	TaskId _localTaskId = 0;
	mutable QByteArray _imageFormat;
	mutable QPixmap _imagePixmap;

};

class StorageImageLocation;
class mtpFileLoader : public FileLoader, public RPCSender {
	Q_OBJECT

public:
	mtpFileLoader(const StorageImageLocation *location, int32 size, LoadFromCloudSetting fromCloud, bool autoLoading);
	mtpFileLoader(int32 dc, const uint64 &id, const uint64 &access, int32 version, LocationType type, const QString &toFile, int32 size, LoadToCacheSetting toCache, LoadFromCloudSetting fromCloud, bool autoLoading);

	int32 currentOffset(bool includeSkipped = false) const override;

	uint64 objId() const override {
		return _id;
	}

	void stop() override {
		rpcClear();
	}

	~mtpFileLoader();

protected:
	bool tryLoadLocal() override;
	void cancelRequests() override;

	typedef QMap<mtpRequestId, int32> Requests;
	Requests _requests;

	bool loadPart() override;
	void partLoaded(int32 offset, const MTPupload_File &result, mtpRequestId req);
	bool partFailed(const RPCError &error);

	bool _lastComplete = false;
	int32 _skippedBytes = 0;
	int32 _nextRequestOffset = 0;

	int32 _dc;
	const StorageImageLocation *_location = nullptr;

	uint64 _id = 0; // for other locations
	uint64 _access = 0;
	int32 _version = 0;

};

class webFileLoaderPrivate;

class webFileLoader : public FileLoader {
	Q_OBJECT

public:

	webFileLoader(const QString &url, const QString &to, LoadFromCloudSetting fromCloud, bool autoLoading);

	virtual int32 currentOffset(bool includeSkipped = false) const;
	virtual webFileLoader *webLoader() {
		return this;
	}
	virtual const webFileLoader *webLoader() const {
		return this;
	}

	void onProgress(qint64 already, qint64 size);
	void onFinished(const QByteArray &data);
	void onError();

	virtual void stop() {
		cancelRequests();
	}

	~webFileLoader();

protected:

	virtual void cancelRequests();
	virtual bool tryLoadLocal();
	virtual bool loadPart();

	QString _url;

	bool _requestSent;
	int32 _already;

	friend class WebLoadManager;
	webFileLoaderPrivate *_private;

};

enum WebReplyProcessResult {
	WebReplyProcessError,
	WebReplyProcessProgress,
	WebReplyProcessFinished,
};

class WebLoadManager : public QObject {
	Q_OBJECT

public:

	WebLoadManager(QThread *thread);

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	void setProxySettings(const QNetworkProxy &proxy);
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

	void append(webFileLoader *loader, const QString &url);
	void stop(webFileLoader *reader);
	bool carries(webFileLoader *reader) const;

	~WebLoadManager();

signals:
	void processDelayed();
	void proxyApplyDelayed();

	void progress(webFileLoader *loader, qint64 already, qint64 size);
	void finished(webFileLoader *loader, QByteArray data);
	void error(webFileLoader *loader);

public slots:
	void onFailed(QNetworkReply *reply);
	void onFailed(QNetworkReply::NetworkError error);
	void onProgress(qint64 already, qint64 size);
	void onMeta();

	void process();
	void proxyApply();
	void finish();

private:
	void clear();
	void sendRequest(webFileLoaderPrivate *loader, const QString &redirect = QString());
	bool handleReplyResult(webFileLoaderPrivate *loader, WebReplyProcessResult result);

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	QNetworkProxy _proxySettings;
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	QNetworkAccessManager _manager;
	typedef QMap<webFileLoader*, webFileLoaderPrivate*> LoaderPointers;
	LoaderPointers _loaderPointers;
	mutable QMutex _loaderPointersMutex;

	typedef OrderedSet<webFileLoaderPrivate*> Loaders;
	Loaders _loaders;

	typedef QMap<QNetworkReply*, webFileLoaderPrivate*> Replies;
	Replies _replies;

};

class WebLoadMainManager : public QObject {
	Q_OBJECT

public:

public slots:

	void progress(webFileLoader *loader, qint64 already, qint64 size);
	void finished(webFileLoader *loader, QByteArray data);
	void error(webFileLoader *loader);

};

static FileLoader * const CancelledFileLoader = SharedMemoryLocation<FileLoader, 0>();
static mtpFileLoader * const CancelledMtpFileLoader = static_cast<mtpFileLoader*>(CancelledFileLoader);
static webFileLoader * const CancelledWebFileLoader = static_cast<webFileLoader*>(CancelledFileLoader);
static WebLoadManager * const FinishedWebLoadManager = SharedMemoryLocation<WebLoadManager, 0>();

void reinitWebLoadManager();
void stopWebLoadManager();
