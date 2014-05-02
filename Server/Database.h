#ifndef DATBASE_H
#define DATBASE_H
#include "item.h"
#include <functional>
#include <unordered_map>
#include <map>
#include <cstring>
#include <algorithm>
#include <assert.h>
#define ID3LIB_LINKOPTION 1
#include <id3/tag.h>
#include <QString>
#include <QDebug>

#define cmp_class(Name,Type,field) struct Name\
{\
	bool operator()(const Type* const& a, const Type* const& b)\
	{\
		if(a->field)\
		{\
			if(b->field)\
				return strcasecmp(a->field,b->field)<0;\
			else\
				return 1;\
		}\
		else\
		{\
			if(b->field)\
				return 0;\
			else\
				return 1;\
		}\
	}\
}

cmp_class(SongTitle,Song,title);
cmp_class(ArtistName,Artist,name);
cmp_class(AlbumName,Album,name);

enum MetaItem
{
	ARTIST,
	ALBUM,
	TITLE,
	DURATION,
};

struct ItemFilter
{
	enum MetaItem field;
	const char* value;
};

//the database will have its add_*, update_song, and delete_song methods called by the server whenever the FolderList sees a change in the set of songs on disk.
//it will call updated_cb after a song and its album/artist have been updated, and will call deleted_cb after a song has been deleted.
struct Database
{
	std::function<void(const Song*)> updated_cb;
	std::function<void(id)> deleted_cb;
	Database(std::function<void(const Song*)> updated, std::function<void(id)> deleted)
	{
		this->updated_cb=updated;
		this->deleted_cb=deleted;
	};//functions to call when a song is updated or deleted
	
		#define lookup(in)\
			if(n == 0) return NULL;\
			auto it=in.find(n);\
			if(it == in.end()) return NULL;\
			return std::get<1>(*it);
			
	static void addSongFromPath(QString path, Database& db)
	{
		if(path.endsWith(".mp3"))
		{
			const char* song = NULL;
			const char* album = NULL;
			const char* artist = NULL;
			unsigned int index = 0;
			unsigned int duration = 0;
			//get id3 info and add song to database
			ID3_Tag tag(path.toUtf8().constData());
			if(tag.NumFrames() > 0)
			{
				//get song title
				ID3_Frame* frame = tag.Find(ID3FID_TITLE);
				if( NULL != frame)
				{
					ID3_Field* field = frame->GetField(ID3FN_TEXT);
					song = field->GetRawText();
				}
				qDebug() << "title: "<<song;
				//get album name
				frame = tag.Find(ID3FID_ALBUM);
				if( NULL != frame)
				{
					ID3_Field* field = frame->GetField(ID3FN_TEXT);
					album = field->GetRawText();
				}
				qDebug() << "album: "<<album;
				//get artist name
				frame = tag.Find(ID3FID_LEADARTIST);
				if( NULL != frame)
				{
					ID3_Field* field = frame->GetField(ID3FN_TEXT);
					artist = field->GetRawText();
				}
				qDebug() << "artist: "<<artist;
				//get song index on album
				frame = tag.Find(ID3FID_TRACKNUM);
				if( NULL != frame)
				{
					ID3_Field* field = frame->GetField(ID3FN_TEXT);
					const char* temp = field->GetRawText();
					sscanf(temp, "%d/",&index);
				}
				qDebug() << "index: "<<index;
				qDebug() << "\n";
			}
			if(song == NULL)
			{
				song = path.toUtf8().constData();
			}
			
			id artistId;
			if(artist != NULL )
				artistId = db.add_artist(artist);
			else
				artistId = 0;
				
			id albumId;
			if(album != NULL )
				albumId = db.add_artist(album);
			else
				albumId = 0;
				
			id newId = db.add_song();
			db.update_song(newId, song, artistId, albumId, index, duration);
		}
	}
	
	Song* find_song(id n) const { lookup(song_ids) };//may be NULL
	Album* find_album(id n) const { lookup(album_ids) };//may be NULL
	const Artist* find_artist(id n) const { lookup(artist_ids) };//may be NULL
	void delete_song(id n)
	{
		auto sit=song_ids.find(n);
		assert(sit != song_ids.end());
		auto s=std::get<1>(*sit);
		
		auto ait=album_ids.find(s->get_album());
		if(ait != album_ids.end())
		{
			//if the song's album reaches 0 tracks, delete it
			auto a=std::get<1>(*ait);
			std::remove(a->tracks.begin(), a->tracks.end(), n);
			if(a->get_n_tracks() == 0)
			{
				std::remove(a->tracks.begin(), a->tracks.end(), n);
				albums.erase(a);
				album_ids.erase(ait);
				delete a;
			}
		}
		
		//if the song's artist reaches 0 tracks, delete it
		delete s;
		deleted_cb(n);
	};
	
	//may be NULL, returns the file data for playback... maybe this should return a FILE* or other readable interface?
	const char* get_song_data(id n)
	{
		const Song* s=find_song(n);
		if(!s) return NULL;
	};
	
	//creates the album if it doesn't exist, and returns the id for an album with that title
	id add_album(const char* name)
	{
		++album_id;
		album_ids[album_id]=new Album(album_id, name?strdup(name):nullptr);
		albums[album_ids[album_id]]=true;
		return album_id;
	};
	
	//creates the artist if it doesn't exist, and returns the id for an artist with that name
	id add_artist(const char* name)
	{
		++artist_id;
		artist_ids[artist_id]=new Artist(artist_id, name?strdup(name):nullptr);
		artists[artist_ids[artist_id]]=true;
		return artist_id;
	};
	
	//creates a new song with no info
	id add_song()
	{
		++song_id;
		printf("added song %u\n", song_id);
		song_ids[song_id]=new Song(song_id, 0, 0, nullptr);
		songs[song_ids[song_id]]=true;
		return song_id;
	};
	
	//fill in song info, maybe replacing old info
	void update_song(id which, const char* title, id artist, id album, unsigned int album_index, unsigned int duration)
	{
		Song* s=find_song(which);
		if(!s)
		{
			assert(0 && "updating nonexistent song!");
			return;
		}
		
		if(s->title)
		{
			delete s->title;
			s->title=nullptr;
		}
		s->title=strdup(title);
		s->artist=artist;
		s->album=album;
		
		Album* b=find_album(s->album);
		if(b)
		{
			b->set_id_at(album_index, which);
		}
		updated_cb(s);
	};
	
	std::vector<Song*> list_songs(std::vector<ItemFilter>& filt, int start, int length)
	{
		printf("called list_songs\n");
		std::vector<Song*> results;
		for(auto i=song_ids.begin(); i!=song_ids.end(); ++i)
		{
			auto song=std::get<1>(*i);
			printf("song %s\n", song->title);
			auto matches=true;
			unsigned int j;
			for(j=0; j<filt.size(); j++)
			{
				printf("checking %s\n", filt[j].value);
				switch(filt[j].field)
				{
					case ARTIST:
						//TODO: nyi
						matches&=false;
						break;
					case ALBUM:
						//TODO: nyi
						matches&=false;
						break;
					case TITLE:
						matches&=song->title && !!strstr(song->title, filt[j].value);
						break;
					case DURATION:
						//TODO: nyi
						matches&=false;
						break;
				}
				if(!matches)
					break;
			}
			if(matches)
				results.push_back(song);
		}
		return results;
	}

#define list_simple(Type,name,field) std::vector<Type*> list_##name##s(const char* query, int start, int length)\
{\
	std::vector<Type*> results;\
	for(auto i=name##_ids.begin(); i!=name##_ids.end(); ++i)\
	{\
		auto value=std::get<1>(*i);\
		printf("checking %s for %s\n", value->field, query);\
		if(!!strstr(value->field, query))\
			results.push_back(value);\
	}\
	return results;\
}

	list_simple(Album,album,name)
	list_simple(Artist,artist,name)
	
	std::unordered_map<id,Song*> song_ids;
	std::map<Song*,bool,SongTitle> songs;
	id song_id=0;
	std::unordered_map<id,Album*> album_ids;
	std::map<Album*,bool,AlbumName> albums;
	id album_id=0;
	std::unordered_map<id,Artist*> artist_ids;
	std::map<Artist*,bool,ArtistName> artists;
	id artist_id=0;
};
#endif
