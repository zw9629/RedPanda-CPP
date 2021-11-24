#include "gdbmiresultparser.h"

#include <QFileInfo>
#include <QList>
#include <QDebug>

GDBMIResultParser::GDBMIResultParser()
{
    mResultTypes.insert("bkpt",GDBMIResultType::Breakpoint);
    mResultTypes.insert("BreakpointTable",GDBMIResultType::BreakpointTable);
    mResultTypes.insert("stack",GDBMIResultType::FrameStack);
    mResultTypes.insert("variables", GDBMIResultType::LocalVariables);
    mResultTypes.insert("frame",GDBMIResultType::Frame);
    mResultTypes.insert("asm_insns",GDBMIResultType::Disassembly);
    mResultTypes.insert("value",GDBMIResultType::Evaluation);
    mResultTypes.insert("register-names",GDBMIResultType::RegisterNames);
    mResultTypes.insert("register-values",GDBMIResultType::RegisterValues);
    mResultTypes.insert("memory",GDBMIResultType::Memory);
}

bool GDBMIResultParser::parse(const QByteArray &record, GDBMIResultType &type, ParseValue& value)
{
    const char* p = record.data();
    QByteArray name;
    bool result = parseNameAndValue(p,name,value);
    if (!result)
        return false;
//    if (*p!=0)
//        return false;
    if (!mResultTypes.contains(name))
        return false;
    type = mResultTypes[name];
    return true;
}

bool GDBMIResultParser::parseAsyncResult(const QByteArray &record, QByteArray &result, ParseObject &multiValue)
{
    const char* p =record.data();
    if (*p!='*')
        return false;
    p++;
    const char* start=p;
    while (*p && *p!=',')
        p++;
    result = QByteArray(start,p-start);
    if (*p==0)
        return true;
    p++;
    return parseMultiValues(p,multiValue);
}

bool GDBMIResultParser::parseMultiValues(const char* p, ParseObject &multiValue)
{
    qDebug()<<"-------";
    qDebug()<<QByteArray(p);
    while (*p) {
        QByteArray propName;
        ParseValue propValue;
        bool result = parseNameAndValue(p,propName,propValue);
        if (result) {
            multiValue[propName]=propValue;
        } else {
            return false;
        }
        skipSpaces(p);
        if (*p==0)
            break;
        if (*p!=',')
            return false;
        p++; //skip ','
        skipSpaces(p);
    }
    return true;
}

bool GDBMIResultParser::parseNameAndValue(const char *&p, QByteArray &name, ParseValue &value)
{
    skipSpaces(p);
    const char* nameStart =p;
    while (*p!=0 && isNameChar(*p)) {
        p++;
    }
    if (*p==0)
        return false;
    name = QByteArray(nameStart,p-nameStart);
    skipSpaces(p);
    if (*p!='=')
        return false;
    p++;
    return parseValue(p,value);
}

bool GDBMIResultParser::parseValue(const char *&p, ParseValue &value)
{
    skipSpaces(p);
    bool result;
    switch (*p) {
    case '{': {
        ParseObject obj;
        result = parseObject(p,obj);
        value = obj;
        break;
    }
    case '[': {
        QList<ParseValue> array;
        result = parseArray(p,array);
        value = array;
        break;
    }
    case '"': {
        QByteArray s;
        result = parseStringValue(p,s);
        value = s;
        break;
    }
    default:
        return false;
    }
    if (!result)
        return false;
    skipSpaces(p);
    return true;
}

bool GDBMIResultParser::parseStringValue(const char *&p, QByteArray& stringValue)
{
    if (*p!='"')
        return false;
    p++;
    stringValue.clear();
    while (*p!=0) {
        if (*p == '"') {
            break;
        } else if (*p=='\\' && *(p+1)!=0) {
            p++;
            switch (*p) {
            case '\'':
                stringValue+=0x27;
                p++;
                break;
            case '"':
                stringValue+=0x22;
                p++;
                break;
            case '?':
                stringValue+=0x3f;
                p++;
                break;
            case '\\':
                stringValue+=0x5c;
                p++;
                break;
            case 'a':
                stringValue+=0x07;
                p++;
                break;
            case 'b':
                stringValue+=0x08;
                p++;
                break;
            case 'f':
                stringValue+=0x0c;
                p++;
                break;
            case 'n':
                stringValue+=0x0a;
                p++;
                break;
            case 'r':
                stringValue+=0x0d;
                p++;
                break;
            case 't':
                stringValue+=0x09;
                p++;
                break;
            case 'v':
                stringValue+=0x0b;
                p++;
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            {
                int i=0;
                for (i=0;i<3;i++) {
                    if (*(p+i)<'0' || *(p+i)>'7')
                        break;
                }
                QByteArray numStr(p,i);
                bool ok;
                unsigned char ch = numStr.toInt(&ok,8);
                stringValue+=ch;
                p+=i;
                break;
            }
            }
        } else {
            stringValue+=*p;
            p++;
        }
    }
    if (*p=='"') {
        p++; //skip '"'
        return true;
    }
    return false;
}

bool GDBMIResultParser::parseObject(const char *&p, ParseObject &obj)
{
    if (*p!='{')
        return false;
    p++;

    if (*p!='}') {
        while (*p!=0) {
            QByteArray propName;
            ParseValue propValue;
            bool result = parseNameAndValue(p,propName,propValue);
            qDebug()<<result<<propName<<QByteArray(p);
            if (result) {
                obj[propName]=propValue;
            } else {
                return false;
            }
            skipSpaces(p);
            if (*p=='}')
                break;
            if (*p!=',') {
                return false;
            }
            p++; //skip ','
            skipSpaces(p);
        }
    }
    if (*p=='}') {
        p++; //skip '}'
        return true;
    }
    return false;
}

bool GDBMIResultParser::parseArray(const char *&p, QList<GDBMIResultParser::ParseValue> &array)
{
    if (*p!='[')
        return false;
    p++;
    if (*p!=']') {
        while (*p!=0) {
            skipSpaces(p);
            ParseValue val;
            bool result = parseValue(p,val);
            if (result) {
                array.append(val);
            } else {
                return false;
            }
            skipSpaces(p);
            if (*p==']')
                break;
            if (*p!=',')
                return false;
            p++; //skip ','
            skipSpaces(p);
        }
    }
    if (*p==']') {
        p++; //skip ']'
        return true;
    }
    return false;
}

bool GDBMIResultParser::isNameChar(char ch)
{
    if (ch=='-')
        return true;
    if (ch>='a' && ch<='z')
        return true;
    if (ch>='A' && ch<='Z')
        return true;
    return false;
}

bool GDBMIResultParser::isSpaceChar(char ch)
{
    switch(ch) {
    case ' ':
    case '\t':
        return true;
    }
    return false;
}

void GDBMIResultParser::skipSpaces(const char *&p)
{
    while (*p!=0 && isSpaceChar(*p))
        p++;
}

const QByteArray &GDBMIResultParser::ParseValue::value() const
{
    Q_ASSERT(mType == ParseValueType::Value);
    return mValue;
}

const QList<::GDBMIResultParser::ParseValue> &GDBMIResultParser::ParseValue::array() const
{
    Q_ASSERT(mType == ParseValueType::Array);
    return mArray;
}

const GDBMIResultParser::ParseObject &GDBMIResultParser::ParseValue::object() const
{
    Q_ASSERT(mType == ParseValueType::Object);
    return mObject;
}

int GDBMIResultParser::ParseValue::intValue(int defaultValue) const
{
    Q_ASSERT(mType == ParseValueType::Value);
    bool ok;
    int value = QString(mValue).toInt(&ok);
    if (ok)
        return value;
    else
        return defaultValue;
}

int GDBMIResultParser::ParseValue::hexValue(int defaultValue) const
{
    Q_ASSERT(mType == ParseValueType::Value);
    bool ok;
    int value = QString(mValue).toInt(&ok,16);
    if (ok)
        return value;
    else
        return defaultValue;
}

QString GDBMIResultParser::ParseValue::pathValue() const
{
    Q_ASSERT(mType == ParseValueType::Value);
    return QFileInfo(QString::fromLocal8Bit(mValue)).absoluteFilePath();
}

GDBMIResultParser::ParseValueType GDBMIResultParser::ParseValue::type() const
{
    return mType;
}

bool GDBMIResultParser::ParseValue::isValid() const
{
    return mType!=ParseValueType::NotAssigned;
}

GDBMIResultParser::ParseValue::ParseValue():
    mType(ParseValueType::NotAssigned) {

}

GDBMIResultParser::ParseValue::ParseValue(const QByteArray &value):
    mValue(value),
    mType(ParseValueType::Value)
{
}

GDBMIResultParser::ParseValue::ParseValue(const ParseObject &object):
    mObject(object),
    mType(ParseValueType::Object)
{
}

GDBMIResultParser::ParseValue::ParseValue(const QList<ParseValue> &array):
    mArray(array),
    mType(ParseValueType::Array)
{
}

GDBMIResultParser::ParseValue::ParseValue(const ParseValue &value):
    mValue(value.mValue),
    mArray(value.mArray),
    mObject(value.mObject),
    mType(value.mType)
{
}

GDBMIResultParser::ParseValue &GDBMIResultParser::ParseValue::operator=(const GDBMIResultParser::ParseValue &value)
{
    mType = value.mType;
    mValue = value.mValue;
    mArray = value.mArray;
    mObject = value.mObject;
    return *this;
}

GDBMIResultParser::ParseValue &GDBMIResultParser::ParseValue::operator=(const QByteArray &value)
{
    Q_ASSERT(mType == ParseValueType::NotAssigned);
    mType = ParseValueType::Value;
    mValue = value;
    return *this;
}

GDBMIResultParser::ParseValue &GDBMIResultParser::ParseValue::operator=(const ParseObject& object)
{
    Q_ASSERT(mType == ParseValueType::NotAssigned);
    mType = ParseValueType::Object;
    mObject = object;
    return *this;
}

GDBMIResultParser::ParseValue &GDBMIResultParser::ParseValue::operator=(const QList<ParseValue>& array)
{
    Q_ASSERT(mType == ParseValueType::NotAssigned);
    mType = ParseValueType::Array;
    mArray = array;
    return *this;
}


GDBMIResultParser::ParseObject::ParseObject()
{

}

GDBMIResultParser::ParseObject::ParseObject(const ParseObject &object):
    mProps(object.mProps)
{

}

GDBMIResultParser::ParseValue GDBMIResultParser::ParseObject::operator[](const QByteArray &name) const
{
    if (mProps.contains(name)) {
        ParseValue value(mProps[name]);
        return value;
    }
    return ParseValue();
}

GDBMIResultParser::ParseObject &GDBMIResultParser::ParseObject::operator=(const ParseObject &object)
{
    mProps = object.mProps;
    return *this;
}

GDBMIResultParser::ParseValue &GDBMIResultParser::ParseObject::operator[](const QByteArray &name) {
    if (!mProps.contains(name))
        mProps[name]=ParseValue();
    return mProps[name];
}

