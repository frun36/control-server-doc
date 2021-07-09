#ifndef IPBUSINTERFACE_H
#define IPBUSINTERFACE_H

#include <QtNetwork>
#include "IPbusHeaders.h"

const quint16 maxPacket = 368; //368 words, limit from ethernet MTU of 1500 bytes
enum errorType {networkError = 0, IPbusError = 1, logicError = 2};
static const char *errorTypeName[3] = {"Network error" , "IPbus error", "Logic error"};

class IPbusTarget: public QObject {
    Q_OBJECT
    const quint16 localport;
    QUdpSocket *qsocket = new QUdpSocket(this);
    const StatusPacket statusRequest;
    StatusPacket statusResponse;
    QList<Transaction> transactionsList;

protected:
    quint16 requestSize = 1, responseSize = 1; //values are measured in words
    quint32 request[maxPacket], response[maxPacket];
    quint32 dt[2]; //temporary data

public:
    QString IPaddress = "172.20.75.180";
    bool isOnline = false;
    QTimer *updateTimer = new QTimer(this);
    quint16 updatePeriod_ms = 1000;

    IPbusTarget(quint16 lport = 0) : localport(lport) {
        request[0] = PacketHeader(control, 0);
        updateTimer->setTimerType(Qt::PreciseTimer);
        connect(updateTimer, &QTimer::timeout, this, [=]() { if (isOnline) sync(); else checkStatus(); });
        connect(this, &IPbusTarget::error, this, [=]() {
            updateTimer->stop();
            resetTransactions();
        });
        if (!qsocket->bind(QHostAddress::AnyIPv4, localport)) qsocket->bind(QHostAddress::AnyIPv4);
        updateTimer->start(updatePeriod_ms);
    }

//    ~IPbusTarget() {
//        qsocket->disconnectFromHost();
//    }

    quint32 readRegister(quint32 address) {
        quint32 data = 0xFFFFFFFF;
        addTransaction(read, address, &data, 1);
        return transceive(true) ? data : 0xFFFFFFFF;
    }

    void log(QString st) {
        qDebug() << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ") + st;
    }

signals:
    void error(QString, errorType);
    void noResponse();
    void IPbusStatusOK();
    void successfulRead(quint8 nWords);
    void successfulWrite(quint8 nWords);

protected:
    quint32 *masks(quint32 mask0, quint32 mask1) { //for convinient adding RMWbit transaction
        dt[0] = mask0; //for writing 0's: AND term
        dt[1] = mask1; //for writing 1's: OR term
        return dt;
    }

    void debugPrint() {
        printf("request:\n");
        for (quint16 i=0; i<requestSize; ++i)  printf("%08X\n", request[i]);
        printf("        response:\n");
        for (quint16 i=0; i<responseSize; ++i) printf("        %08X\n", response[i]);
        printf("\n");
    }

    void addTransaction(TransactionType type, quint32 address, quint32 *data, quint8 nWords = 1) {
        Transaction currentTransaction;
        request[requestSize] = TransactionHeader(type, nWords, transactionsList.size());
        currentTransaction.requestHeader = (TransactionHeader *)(request + requestSize++);
        request[requestSize] = address;
        currentTransaction.address = request + requestSize++;
        currentTransaction.responseHeader = (TransactionHeader *)(response + responseSize++);
        switch (type) {
            case                read:
            case nonIncrementingRead:
            case   configurationRead:
                currentTransaction.data = data;
                responseSize += nWords;
                break;
            case                write:
            case nonIncrementingWrite:
            case   configurationWrite:
                currentTransaction.data = request + requestSize;
                for (quint8 i=0; i<nWords; ++i) request[requestSize++] = data[i];
                break;
            case RMWbits:
                request[requestSize++] = data[0]; //AND term
                request[requestSize++] = data[1]; // OR term
                currentTransaction.data = response + responseSize++;
                break;
            case RMWsum:
                request[requestSize++] = *data; //addend
                currentTransaction.data = response + responseSize++;
                break;
            default:
                emit error("unknown transaction type", IPbusError);
        }
        if (requestSize > maxPacket || responseSize > maxPacket) {
            emit error("packet size exceeded", IPbusError);
            return;
        } else transactionsList.append(currentTransaction);
    }

    void addWordToWrite(quint32 address, quint32 value) { addTransaction(write, address, &value, 1); }

    bool transceive(bool shouldResponseBeProcessed = true) { //send request, wait for response, receive it and check correctness
        if (requestSize <= 1) {
            emit error("Empty request", logicError);
            return false;
        }
        qint32 n = qint32(qsocket->write((char *)request, requestSize * wordSize));
		if (n < 0) {
            emit error("Socket write error: " + qsocket->errorString(), networkError);
			return false;
		} else if (n != requestSize * wordSize) {
            emit error("Sending packet failed", networkError);
			return false;
        } else if (!qsocket->waitForReadyRead(100) && !qsocket->hasPendingDatagrams()) {
            isOnline = false;
            emit noResponse();
			return false;
        }
        n = qsocket->readDatagram((char *)response, qsocket->pendingDatagramSize());
        if (n == 64 && response[0] == statusRequest.header) {
            log("unexpected status packet received");
            if (!qsocket->hasPendingDatagrams() && !qsocket->waitForReadyRead(100) && !qsocket->hasPendingDatagrams()) {
                isOnline = false;
                emit noResponse();
                return false;
            }
            n = qsocket->readDatagram((char *)response, qsocket->pendingDatagramSize());
        }
        if (n == 0) {
            emit error("empty response, no IPbus on " + IPaddress, networkError);
            return false;
        } else if (n / wordSize > responseSize || response[0] != request[0] || n % wordSize > 0) {
            emit error(QString::asprintf("incorrect response (%d bytes)", n), networkError);
            return false;
        } else {
            responseSize = quint16(n / wordSize); //response can be shorter then expected if a transaction wasn't successful
            bool result = shouldResponseBeProcessed ? processResponse() : true;
            resetTransactions();
            return result;
        }
	}

    bool processResponse() { //check transactions successfulness and copy read data to destination
        for (quint16 i=0; i<transactionsList.size(); ++i) {
            TransactionHeader *th = transactionsList.at(i).responseHeader;
            if (th->ProtocolVersion != 2 || th->TransactionID != i || th->TypeID != transactionsList.at(i).requestHeader->TypeID) {
                emit error(QString::asprintf("unexpected transaction header: %08X, expected: %08X", *th, *transactionsList.at(i).requestHeader & 0xFFFFFFF0), IPbusError);
                return false;
            }
            if (th->Words > 0) switch (th->TypeID) {
                case                read:
                case nonIncrementingRead:
                case   configurationRead:
                    if (transactionsList.at(i).data != nullptr) {
                        quint32 *src = (quint32 *)th + 1, *dst = transactionsList.at(i).data;
                        while (src <= (quint32 *)th + th->Words && src < response + responseSize) *dst++ = *src++;
                    }
                    if ((quint32 *)th + th->Words >= response + responseSize) { //response too short to contain nWords values
                        emit successfulRead(response + responseSize - (quint32 *)th - 1);
                        emit error("read transaction truncated", IPbusError);
                        return false;
                    } else
                        emit successfulRead(th->Words);
                    break;
                case RMWbits:
                case RMWsum :
                    if (th->Words != 1) {
                        emit error("wrong RMW transaction", IPbusError);
                        return false;
                    }
                    emit successfulRead(1);
                    /* fall through */ //[[fallthrough]];
                case                write:
                case nonIncrementingWrite:
                case   configurationWrite:
                    emit successfulWrite(th->Words);
                    break;
                default:
                    emit error("unknown transaction type", IPbusError);
                    return false;
            }
            if (th->InfoCode != 0) {
                debugPrint();
                emit error(th->infoCodeString() + QString::asprintf(", address: %08X", *transactionsList.at(i).address), IPbusError);
                return false;
            }
        }
        return true;
    }

protected slots:
    void resetTransactions() { //return to initial (default) state
        requestSize = 1;
        responseSize = 1;
        transactionsList.clear();
    }

public slots:
    void checkStatus() {
        qsocket->write((char *)&statusRequest, sizeof(statusRequest));
        if (!qsocket->waitForReadyRead(100) && !qsocket->hasPendingDatagrams()) {
            isOnline = false;
            emit noResponse();
        } else {
            qsocket->read((char *)&statusResponse, qsocket->pendingDatagramSize());
            isOnline = true;
            emit IPbusStatusOK();
        }
    }

    void reconnect() {
        if (qsocket->state() == QAbstractSocket::ConnectedState) {
            qsocket->disconnectFromHost();
        }
        qsocket->connectToHost(IPaddress, 50001, QIODevice::ReadWrite, QAbstractSocket::IPv4Protocol);
        if (!qsocket->waitForConnected() && qsocket->state() != QAbstractSocket::ConnectedState) {
            isOnline = false;
            emit noResponse();
            return;
        }
        checkStatus();
        if (!updateTimer->isActive()) updateTimer->start(updatePeriod_ms);
    }



    virtual void sync() =0;

	void writeRegister(quint32 data, quint32 address, bool syncOnSuccess = true) {
        addTransaction(write, address, &data, 1);
		if (transceive() && syncOnSuccess) sync();
    }

	void setBit(quint8 n, quint32 address, bool syncOnSuccess = true) {
		addTransaction(RMWbits, address, masks(0xFFFFFFFF, 1 << n));
		if (transceive() && syncOnSuccess) sync();
	}

	void clearBit(quint8 n, quint32 address, bool syncOnSuccess = true) {
		addTransaction(RMWbits, address, masks(~(1 << n), 0x00000000));
		if (transceive() && syncOnSuccess) sync();
	}

	void writeNbits(quint32 data, quint32 address, quint8 nbits = 16, quint8 shift = 0, bool syncOnSuccess = true) {
        quint32 mask = (1 << nbits) - 1; //e.g. 0x00000FFF for nbits==12
        addTransaction(RMWbits, address, masks( ~quint32(mask << shift), quint32((data & mask) << shift) ));
		if (transceive() && syncOnSuccess) sync();
    }
};

#endif // IPBUSINTERFACE_H