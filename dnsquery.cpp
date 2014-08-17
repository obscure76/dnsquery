#include <iostream>
#include <cstring>
#include <map>
#include <cmath>
#include <iomanip>
#include <thread>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <config.h>
#include <ldns/ldns.h>
#include <mysql++.h>
#include <ssqls.h>
#include <sys/time.h>
#include <datetime.h>

using namespace boost::asio;
using namespace std;
using namespace mysqlpp;
#define MAXLEN 8
/*
   Top 10 domains to query:
   +------+---------------+
   | rank | name       |
   +------+---------------+
   |   1 | google.com  |
   |   2 | facebook.com  |
   |   3 | youtube.com   |
   |   4 | yahoo.com   |
   |   5 | live.com    |
   |   6 | wikipedia.org |
   |   7 | baidu.com   |
   |   8 | blogger.com   |
   |   9 | msn.com     |
   |  10 | qq.com     |
   +------+---------------+
   =================================
   This Program creates a table with name mytable with fields as
   DOMAIN    COUNT  MeanQTIME  STDDEV    StartTIMESTAMP      LASTTIMESTAMP
   baidu.com   5     251        307.424 2014-08-16 23:13:51  2014-08-16 23:13:51

   The deadline_timer is used for a periodic callback of the main routine
   Structure of the Program:    
      io_service         - To run the timer 

      main               - parses the parameters from the CLI and creates a DnsQuery object 
                           with a timer. Then calls initlize services.

      initializeServices - This initializes the connection with the database and creates 
                           ldns_resolver object necessary for resolving dns queries
      
      timeout            - This is callback routine called once the timer expires. This module
                           calls sendQuery, updateDB and wait() to await its next callback

      sendQuery          - This routine is responsible for sending dns queries for all the 
                           domains, once new data is obtained, updates the record for the 
                           domain in the database

      updateDB           - This routine is responsible for sending SQL queries to DB and 
                           creates and updates the record for each database with the fields
                           mentioned above

      wait               - This routine associates the configured frequency with the timer
                           and waits till the timer expiry to call the callback
 */
class DnsQuery
{
    private:
        map<string, double> doMean;
        map<string, double> doSquare;
        deadline_timer &t;
        int freq;
        list<string> domainlist;
        Connection conn;
        int qcount;
        ldns_resolver *res;
        string dbname;
        string dbserver;
        string dbuser;
        string dbpswd;

        void wait() {
            t.expires_from_now(boost::posix_time::seconds(freq)); //repeat rate here
            t.async_wait(boost::bind(&DnsQuery::timeout, this, boost::asio::placeholders::error));
        }

        void fillDomainList()
        {
            domainlist.push_back("google.com");
            domainlist.push_back("facebook.com");
            domainlist.push_back("youtube.com");
            domainlist.push_back("yahoo.com");
            domainlist.push_back("live.com");
            domainlist.push_back("wikipedia.org");
            domainlist.push_back("baidu.com");
            domainlist.push_back("blogger.com");
            domainlist.push_back("msn.com");
            domainlist.push_back("qq.com");
        }

    public:
        DnsQuery(deadline_timer &timer, int frequency) : t(timer), freq(frequency) {
            /* Constructor */
            qcount = 0;
            fillDomainList();
            printDomainList();
            wait();
        }

        void initializeServices()
        {
            if (conn.connect(dbname.c_str(), dbserver.c_str(), dbuser.c_str(), dbpswd.c_str())) {
                cout<<"Connected to DB";
            } else {
                cout<<"Could not connect to DB";
                exit(-1);
            }
            try {
                Query query = conn.query();
                query << 
                    "  CREATE TABLE IF NOT EXISTS mytable (" <<
                    "  domain CHAR(100) NOT NULL PRIMARY KEY, " <<
                    "  count INT UNSIGNED, " <<
                    "  mean DOUBLE UNSIGNED, " <<
                    "  sumofsq DOUBLE UNSIGNED, "<<
                    "  stddev DOUBLE UNSIGNED, " << 
                    "  startTime  TIMESTAMP , " <<
                    "  endTime  TIMESTAMP ); ";
                query.execute();
                cout<<"Table created";
            } catch (const Exception& er){
                cerr << er.what() << std::endl;
                exit(-1);
            }

            if (ldns_resolver_new_frm_file(&res, NULL) != LDNS_STATUS_OK)
            {
                fprintf(stderr, "%s", "Could not create resolver obj");
                exit(-1);
            }
            ldns_resolver_set_dnssec(res, true);
            ldns_resolver_set_dnssec_cd(res, true);
            uint8_t fam = LDNS_RESOLV_INETANY;
            ldns_resolver_set_ip6(res, fam);	
            if (!res) {
                fprintf(stderr, "%s", "Could not create resolver obj");
                exit(-1);
            }
        }

        ~DnsQuery()
        {
            conn.disconnect();
            ldns_resolver_deep_free(res);
        }

        void setdbname(char *str) {  dbname = string(str); }

        void setdbserver(char *str) {  dbserver = string(str); }

        void setdbuser(char *str) {  dbuser = string(str);}

        void setdbpswd(char *str) { dbpswd = string(str); }

        void printDomainList()
        {
            list<string>::iterator it;
            cout<<"Domain list";
            for(it = domainlist.begin();it!=domainlist.end();it++)
                cout<<*it<<endl;
            cout<<endl;
        }

        void printContents()
        {
            Query query = conn.query("SELECT * FROM mytable");
            if (StoreQueryResult result = query.store()) {
                cout << "We have:" << endl;
                StoreQueryResult::const_iterator it;
                for (it = result.begin(); it != result.end(); ++it) {
                    Row row = *it;
                    cout<< setw(14)  << row["domain"] 
                        << setw(4)   << row["count"]
                        << setw(8)   << row["mean"]
                        << setw(15)  << row["stddev"]
                        << setw(20)  << row["startTime"]
                        << setw(12)  << row["endTime"]
                        <<endl;
                }
            }
            while (query.more_results()){
                query.store_next();
            }
            cout<<endl<<endl;
        }

        void timeout(const boost::system::error_code &e) {
            if (e)
                return;
            cout<<"Enter exit to exit the program";
            srand(time(NULL));
            qcount++;
            sendQuery();
            printContents();
            wait();
        }

        void sendQuery();

        double calcMean(double prevMean,  int currVal)
        {
            return (double)(prevMean*(qcount-1) + (double)currVal)/qcount;
        }

        double calcStddev(double sumOfsq, double mean, int currVal, string domain)
        {
            double a = sumOfsq/qcount;
            double b = mean * mean;
            if(a>=b)
                return sqrt((double)sumOfsq/qcount - mean*mean);
            else
            {
                /* Special case: restart the series */
                doMean.insert(pair<string, double>(domain, currVal));
                doSquare.insert(pair<string, double>(domain, currVal));
                return 0;
            }
        }
        void updateDB(string, int);

        char *getRandomString(string s)
        {
            static const char alphanum[] = "abcdefghijklmnopqrstuvwxyz";
            int randomlen = rand() % MAXLEN +1;
            char *str = (char *)malloc(randomlen+s.size()+2);
            memset(str, 0, randomlen+s.size()+2);
            str[randomlen] = '.';
            for(int i = 0; i<randomlen;i++)
            {
                str[i] = alphanum[rand()%26];
            }
            s.copy(str+randomlen+1,s.size(),0);
            return str;
        }

        void getPrevData(double &mean, double &sumofsq, string domain)
        {
            Query q = conn.query();
            q << "SELECT * FROM mytable "
                << "WHERE domain = "<< quote<<domain;
            q.execute();
            if (StoreQueryResult result = q.store()) {
                StoreQueryResult::const_iterator it;
                for (it = result.begin(); it != result.end(); ++it) {
                    Row row = *it;
                    mean = row["mean"];
                    sumofsq = row["sumofsq"];
                }
            }
            while (q.more_results()){
                q.store_next();
            }
            q.reset();
        }
        void cancel() {
            t.cancel();
        }
};

void DnsQuery::sendQuery()
{
    ldns_rdf *domain = NULL;
    ldns_pkt *p; 
    ldns_rr *soa;
    ldns_rr_list *rrlist;

    p = NULL;
    rrlist = NULL;
    soa = NULL;
    domain = NULL;

    list<string>::iterator it;
    for(it = domainlist.begin();it!=domainlist.end();it++)
    {
        string s;
        int len;
        char *domainstr = getRandomString(*it);
        //cout<<domainstr;
        domain = ldns_dname_new_frm_str(domainstr);
        p = ldns_resolver_query(res, domain, LDNS_RR_TYPE_SOA, LDNS_RR_CLASS_IN, LDNS_RD);
        soa = NULL;
        if (p) {
            //ldns_pkt_print(stdout, p);
            //cout<<"Time"<<ldns_pkt_querytime(p);
            updateDB(*it,ldns_pkt_querytime(p));
        } else {
            fprintf(stdout, "No Packet Received for %s\n", domainstr);
            continue;
        }
        if (!rrlist || ldns_rr_list_rr_count(rrlist) != 1) {
            ldns_pkt_free(p);
        } else {
            soa = ldns_rr_clone(ldns_rr_list_rr(rrlist, 0));
            ldns_rr_list_deep_free(rrlist);
            rrlist = NULL;
            rrlist = ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_RRSIG, LDNS_SECTION_ANSWER);
            if (!rrlist) {
                ldns_pkt_free(p);
                continue;
            }
            ldns_rr_list_deep_free(rrlist);
        }
    }
    cout<<"\n\n";
}


void DnsQuery::updateDB(string domain, int querytime)
{
    Query query = conn.query();
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    static char timebuf[sizeof("0000-00-00 00:00:00")];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %T", timeinfo);

    if(qcount == 1)
    {
        /* First time Replace */
        query << "replace into mytable values("
            << quote << domain
            << "," << qcount
            << "," << querytime 
            << "," << (querytime * querytime) 
            << "," << querytime 
            << "," << quote << timebuf
            << "," << quote << timebuf
            << ")";
        doMean.insert(pair<string, double>(domain, querytime));
        doSquare.insert(pair<string, double> (domain, querytime*querytime));
    } else {
        /* Now update the values */
        double prevSum=doMean.find(domain)->second;
        double prevsumofsq=doSquare.find(domain)->second;
        query.reset();
        double newMean = (prevSum + querytime)/qcount;
        double newsumofsq = prevsumofsq + (querytime * querytime);
        doMean.insert(pair<string, double>(domain, prevSum + querytime));
        doSquare.insert(pair<string, double>(domain, newsumofsq));
        query << "update mytable " << " set"
            << "  count=" << qcount 
            << " ,mean=" << newMean
            << " ,sumofsq=" << newsumofsq
            << " ,stddev=" << calcStddev(newsumofsq, newMean, querytime, domain)
            << " ,endTime=" << quote << timebuf
            << " where domain=" << quote << domain;
    }
    try {
        query.execute();
    } catch(const Exception &er) {
        cout<<query<<"failed";
        cerr<<er.what()<<endl;
    }
}

void exit_program()
{
    string str;
    while(1)
    {
        cin>>str;
        if(str == "exit")
            exit(0);
        else
            continue;
    }
}

int main(int argc, char **argv)
{

    io_service io;
    int frequency;
    if(argc <6)
    {
        cout<<"Usage: dnsquery  freq  dbname  dbserver  user  pswd";
        return 0;
    }
    cout<<atoi(argv[1]);
    deadline_timer t(io);
    DnsQuery dq(t, (int)argv[1]);
    dq.setdbname(argv[2]);
    dq.setdbserver(argv[3]);
    dq.setdbuser(argv[4]);
    dq.setdbpswd(argv[5]);
    dq.initializeServices();
    thread exitthread (exit_program);
    io.run();
    return 0;
}
