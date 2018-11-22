#ifndef AD9834_H
#define AD9834_H 1

class AD9834 {

    public:

	bool adopen(string dev);
	bool adreset(void);
	bool adsetfreq(int f);
	void adclose(void);

    private:

	bool adwrite(unsigned char *data, int len);

	int fd = -1;
	int fsel = 0;
};

#endif


