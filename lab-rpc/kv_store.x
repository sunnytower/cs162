typedef char buf <>;
struct key_value {
	buf key;
	buf value;
};
typedef struct key_value key_value;

 program KVSTORE {
	version KVSTORE_V1 {
		int EXAMPLE(int) = 1;
		string ECHO(string) = 2;
		void PUT(key_value) = 3;
		buf GET(buf) = 4;
	} = 1;
} = 0x20000001;
