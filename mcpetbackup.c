#include <stdio.h>
#include <getopt.h>

#include "nbt.h"
#include "chunk.h"

enum paramIndex
{
    UNKNOWN = 0,
    REGION,
    SAVE,
    LOAD,
    NAME,
    OWNER
};

static struct option longopts[] = {
    { "regiondata", required_argument,            NULL,           REGION    },
    { "save",       required_argument,            NULL,           SAVE      },
    { "load",       required_argument,            NULL,           LOAD      },
    { "name",       required_argument,            NULL,           NAME      },
    { "owner",      required_argument,            NULL,           OWNER     },
    { NULL,         0,                            NULL,           UNKNOWN   }
};

int savePetToFile(Tag* pet, const char* filename) {
    void* petData;
    size_t petDataLength = composeTag(*pet, &petData);
    
    int fd = open(filename,O_CREAT|O_WRONLY,0644);
    if(fd == -1) {
        fprintf(stderr,"Unable to open file to save pet: %s\n",strerror(errno));
        free(petData);
        return 1;
    }
    ssize_t nWritten = 0;
    size_t totalWritten = 0;

    while((nWritten = write(fd,petData+totalWritten,petDataLength-totalWritten))) {
        if(nWritten == -1) {
            if(errno == EINTR) {
                continue;
            }
            fprintf(stderr,"Unable to write pet data: %s\n",strerror(errno));
            close(fd);
            free(petData);
            return 2;
        }
        totalWritten += nWritten;
    }
    close(fd);

    free(petData);
    return 0;
}

int loadPetFromFile(Tag* pet, const char* filename) {
    int fd = open(filename,O_RDONLY);
    if(fd == -1) {
        fprintf(stderr,"Unable to open file to load pet: %s\n",strerror(errno));
        return 1;
    }

    struct stat sb;
    if(stat(filename,&sb) == -1) {
        fprintf(stderr,"Unable to stat() file to load pet: %s\n",strerror(errno));
        close(fd);
        return 2;
    }
    void* petData = calloc(sb.st_size,sizeof(uint8_t));

    ssize_t nRead = 0;
    size_t totalRead = 0;

    while((nRead = read(fd,petData+totalRead,sb.st_size-totalRead))) {
        if(nRead == -1) {
            if(errno == EINTR) {
                continue;
            }
            fprintf(stderr,"Unable to write pet data: %s\n",strerror(errno));
            close(fd);
            free(petData);
            return 3;
        }
        totalRead += nRead;
    }
    close(fd);

    size_t pos = parseTag(petData,pet);
    if(pos != sb.st_size) {
        fprintf(stderr,"Error parsing pet data\n");
        free(petData);
        return 4;
    }

    free(petData);
    return 0;
}

int getEntitiesTag(TagCompound* chunkRoot,Tag** entities) {
    Tag* t;
    int found = 0;
    for(int i = 0; i < chunkRoot->numTags; ++i) {
        t = &(chunkRoot->list[i]);
        if(!strcmp(t->name,"Level")) {
            found = 1;
            break;
        }
    }
    if(!found) {
        fprintf(stderr,"Unable to locate Level tag\n");
        return 1;
    }
    found = 0;
    
    TagCompound* level = t->payload;
    for(int i = 0; i < level->numTags; ++i) {
        t = &(level->list[i]);
        if(!strcmp(t->name,"Entities")) {
            found = 1;
            *entities = t;
            break;
        }
    }
    if(!found) {
        fprintf(stderr,"Unable to locate Entities tag\n");
        return 2;
    }
    *entities = t;
    return 0;
}

int searchForPet(Tag entities, const char* petName, const char* ownerUUID, Tag** pet) {
    if(petName == NULL && ownerUUID == NULL) {
        fprintf(stderr,"At least petName or ownerUUID have to be non-NULL\n");
        return -1;
    }
    
    Tag* t;
    TagList* entitiesList = entities.payload;
    TagCompound* entity;
    for(int i = 0; i < entitiesList->size; ++i) {
        t = &(entitiesList->list[i]);
        entity = t->payload;
        for(int j = 0; j < entity->numTags; ++j) {
            Tag* attr = &(entity->list[j]);
            if(!strcmp(attr->name,"OwnerUUID") && ownerUUID != NULL) {
                if(!strcmp(attr->payload,ownerUUID)) {
                    *pet = t;
                    return 0;
                }
            } else if(!strcmp(attr->name,"CustomName") && petName != NULL) {
                if(!strcmp(attr->payload,petName)) {
                    *pet = t;
                    return 0;
                }
            }
        }
    }
    return 1;
}

ssize_t insertPetIntoChunk(void** chunkData, Tag chunkRoot, Tag* entities, Tag pet, double x, double y, double z) {
    TagList* entitiesList = (TagList*)entities->payload;
    if(entitiesList->type == TAG_END) {
        // Entities was an empty list. Generating it
        entitiesList->type = TAG_COMPOUND;
        entitiesList->size = 0;
        entitiesList->list = calloc(1,sizeof(uint8_t));
    }
    void* newptr = reallocarray(entitiesList->list,++entitiesList->size,sizeof(Tag));
    if(newptr == NULL) {
        fprintf(stderr,"Unable to realloc an additional entity on the entities list\n");
        return -1;
    }
    entitiesList->list = newptr;
    Tag* newPet = &entitiesList->list[(entitiesList->size) - 1];
    
    void* rawPetData;
    size_t rawPetDataLength = composeTag(pet,&rawPetData);
    size_t parsePos = parseTag(rawPetData,newPet);
    if(parsePos != rawPetDataLength) {
        fprintf(stderr,"Error while duplicating pet. New pet data does not match original\n");
        free(rawPetData);
        return -2;
    }
    free(rawPetData);

    void* newChunkData;
    size_t chunkDataLength = composeTag(chunkRoot, &newChunkData);
    newptr = realloc(*chunkData,chunkDataLength);
    if(newptr == NULL) {
        fprintf(stderr,"Unable to realloc chunkData to fit new data\n");
        free(newChunkData);
        return -4;
    }
    *chunkData = newptr;
    memcpy(*chunkData,newChunkData,chunkDataLength);
    free(newChunkData);

    return chunkDataLength;
}

void printHelp() {
    fprintf(stderr,"--regiondata ./industrial/world/region --save Iris.mcdata --name Iris\n");
    fprintf(stderr,"--regiondata ./industrial/world/region --save Iris.mcdata --owner 32812f90-17ec-4f5a-8b7e-e500f17b1ba5\n");
    fprintf(stderr,"--regiondata ./industrial/world/region --load Iris.mcdata\n");
}

int main(int argc, char** argv) {
    const char* regionFolder = NULL;
    const char* file = NULL;
    const char* petName = NULL;
    const char* ownerUUID = NULL;
    int save = 0;
    
    int longIndex = UNKNOWN;
    int c;

    if(argc < 2) {
        fprintf(stderr,"No options specified\n\n");
        fprintf(stderr,"Examples:\n");
        printHelp();
        return 0;
    }

    while ((c = getopt_long(argc, argv, "r:s:l:n:o:h", longopts, &longIndex)) != -1)
    {
        if(c == 'h') {
            printHelp();
            return 0;
        }
        else if(c == REGION) {regionFolder = optarg;}
        else if(c == SAVE) {save = 1;file = optarg;}
        else if(c == LOAD) {file = optarg;}
        else if(c == NAME) {petName = optarg;}
        else if(c == OWNER) {ownerUUID = optarg;}
        else {
            fprintf(stderr,"Unrecognised argument: %s\n",optarg);
            printHelp();
            return 1;
        }
    }
    if(optind != argc) {
        fprintf(stderr,"Unrecognised argument: %s\n",argv[optind+1]);
        printHelp();
        return 1;
    }

    if(regionFolder == NULL) {
        fprintf(stderr,"Region path not specified (--regionpath PATH_TO_REGION_FOLDER)\n");
        printHelp();
        return 2;
    } else if(save && petName == NULL && ownerUUID == NULL) {
        fprintf(stderr,"OwnerUUID and petName were unspecified (--owner UUID, --name PET_NAME)\n");
        printHelp();
        return 3;
    } else if(!save && (petName != NULL || ownerUUID != NULL)) {
        fprintf(stderr,"OwnerUUID and petName were unspecified (--owner UUID, --name PET_NAME)\n");
        printHelp();
        return 3;
    } else if(file == NULL) {
        fprintf(stderr,"Neither saving or loading a pet was requested (--save PATH_TO_FILE, --load PATH_TO_FILE)\n");
        printHelp();
        return 4;
    }

    void *chunkData;
    ChunkID chunk = translateCoordsToChunk(800, 200, 3041);
    ssize_t chunkLen = loadChunk(regionFolder, chunk, &chunkData);
    if(chunkLen <= 0) {
        return chunkLen;
    }

    Tag t;
    unsigned int pos = parseTag(chunkData,&t);
    if(pos != chunkLen) {
        fprintf(stderr, "Didn't reach end of NBT file\n");
        free(chunkData);
        return 5;
    }

    Tag* entities;
    if(getEntitiesTag((TagCompound*)t.payload,&entities)) {
        fprintf(stderr, "Unable to find Entities tag\n");
        free(chunkData);
        return 6;
    }
    if(save) {    
        Tag* pet;
        if(searchForPet(*entities,petName,ownerUUID,&pet)) {
            fprintf(stderr, "Unable to find pet named %s\n",petName);
            free(chunkData);
            return 7;
        }
        if(savePetToFile(pet,file)) {
            fprintf(stderr, "Unable to save pet to file: %s\n",file);
            free(chunkData);
            return 8;
        }
    } else {
        Tag pet;
        if(loadPetFromFile(&pet,file)) {
            fprintf(stderr, "Unable to load pet from file: %s\n",file);
            free(chunkData);
            return 9;
        }
        chunkLen = insertPetIntoChunk(&chunkData,t,entities,pet,801,200,3040);
        if(chunkLen <= 0) {
            fprintf(stderr, "Unable to insert pet into chunk\n");
            free(chunkData);
            return 10;
        }
        destroyTag(&pet);
        if(overwriteChunk(regionFolder, chunk, chunkData, chunkLen)) {
            fprintf(stderr, "Unable to write new chunk\n");
            free(chunkData);
            return 11;
        }
    }

    destroyTag(&t);
    free(chunkData);
    return 0;
}