#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include "report.h"
#include "put.h"
#include "delete.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    Report *report = new Report;
    report->getAllEvents("https://ec2-175-41-181-116.ap-southeast-1.compute.amazonaws.com/davical/caldav.php/mani/calendar/",
                        "ya29.AHES6ZSTIDrnhbog3SRIbWBzp8IjcAy9dqk7CnIWzfgUgeEMepk-qA");

//    qDebug() << "-----------------------------------Starting MultiGET request for 4 items -------------------------------";
//    QStringList idList;
//    idList << "/caldav/v2/mobilitas123%40gmail.com/events/5pc866q9pd02gfqhkka38b1938%40google.com.ics";
//    idList << "/caldav/v2/mobilitas123%40gmail.com/events/g785upk1g9gev0a3pljdr3a0fs%40google.com.ics";
//    idList << "/caldav/v2/mobilitas123%40gmail.com/events/rvft8rh86c3od7hd1ai52sls88%40google.com.ics";
//    idList << "/caldav/v2/mobilitas123%40gmail.com/events/sk91ip6bkc4i744or97t96l70o%40google.com.ics";
//    idList << "/caldav/v2/mobilitas123%40gmail.com/events/sac4samagmkol4a2j2lirol1g8%40google.com.ics";
//    report->multiGetEvents("https://apidata.googleusercontent.com/caldav/v2/mobilitas123@gmail.com/events/",
//                           "ya29.AHES6ZSuJai_DVqjdu4tDYkvzKlNGrFPaFLzmLEoHuIneSsYlRV5YQ", idList);

//    QString data = "BEGIN:VCALENDAR\nPRODID:-//Google Inc//Google Calendar 70.9054//EN VERSION:2.0\nCALSCALE:GREGORIAN\nX-WR-CALNAME:mobilitas123@gmail.com\nX-WR-TIMEZONE:Europe/London\BEGIN:VTIMEZONE\nTZID:Europe/London\nX-LIC-LOCATION:Europe/London\n" \
//                   "BEGIN:DAYLIGHT\nTZOFFSETFROM:+0000\nTZOFFSETTO:+0100\nTZNAME:BST\nDTSTART:19700329T010000\nRRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU\nEND:DAYLIGHT\nBEGIN:STANDARD\nTZOFFSETFROM:+0100\nTZOFFSETTO:+0000\nTZNAME:GMT\n" \
//                   "DTSTART:19701025T020000\nRRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\nEND:STANDARD\nEND:VTIMEZONE\nBEGIN:VEVENT\nDTSTART;TZID=Europe/London:20121121T080000\nDTEND;TZID=Europe/London:20121121T090000\n" \
//                   "DTSTAMP:20121120T071549Z\nUID:sac4samagmkol4a2j2lirol1g8@google.com\nCREATED:20121120T071549Z\nDESCRIPTION:sdfsdfsdfsdfsfsfsFbfbfbbf\nLAST-MODIFIED:20131002T071549Z\nLOCATION:Hrgrbhjjhjhkjkjhjkjkjkjkjkjkkkjkjljlrb\nSEQUENCE:0\nSTATUS:CONFIRMED\nSUMMARY:manish\nTRANSP:OPAQUE\nCATEGORIES:http://schemas.google.com/g/2005#event\nEND:VEVENT\nEND:VCALENDAR";
//    Put *put = new Put;
//    put->createEvent("https://apidata.googleusercontent.com/caldav/v2/mobilitas123%40gmail.com/events/",
//                     "ya29.AHES6ZSuJai_DVqjdu4tDYkvzKlNGrFPaFLzmLEoHuIneSsYlRV5YQ", "");

//    Delete *del = new Delete;
//    del->deleteEvent("https://apidata.googleusercontent.com/caldav/v2/mobilitas123%40gmail.com/events/g785upk1g9gev0a3pljdr3a0fs%40google.com.ics",
//                     "ya29.AHES6ZRv3tXPU-pSew0UCxyLbYaGtyt6oUIzMtXBAsDU5wPVSDGmgw");

//    QStringList idList;
//    idList << "/caldav/v2/mobilitas123%40gmail.com/events/g785upk1g9gev0a3pljdr3a0fs%40google.com.ics";
//    Report *rep = new Report;
//    rep->multiGetEvents("https://apidata.googleusercontent.com/caldav/v2/mobilitas123@gmail.com/events",
//                        "ya29.AHES6ZRv3tXPU-pSew0UCxyLbYaGtyt6oUIzMtXBAsDU5wPVSDGmgw", idList);

    return a.exec();
}
