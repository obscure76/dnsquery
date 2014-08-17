#include <iostream>
using namespace std;

class dnsQuery
{
    private:
    public:
        dnsQuery()
        {
        }
        void sendquery();
        void calcStats();
        void updateDB();
};

int main(int argc, char **argv)
{
    if(argc <2)
    {
        cout<<"Usage dnsquery <freq in ms>";
        return 0;
    }
    int frequency = (int)*argv[1];
    dnsQuery DQuery(); 
    return 0;
}

