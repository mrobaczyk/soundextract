#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <commdlg.h>
#include <CommCtrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include "resource.h"
#include "tinyxml2.h"

typedef uint32_t UInt32;
typedef uint16_t UInt16;
typedef UInt32 MediaID;
constexpr UInt32 BankHeaderChunkID = 'DHKB';
constexpr UInt32 BankDataIndexChunkID = 'XDID';
constexpr UInt32 BankDataChunkID = 'ATAD';
struct SubchunkHeader
{
	UInt32 dwTag;
	UInt32 dwChunkSize;
};

struct BankHeader
{
	UInt32 dwBankGeneratorVersion;
	UInt32 dwSoundBankID;
	UInt32 dwLanguageID;
	UInt16 bFeedbackInBank;
	UInt16 bDeviceAllocated;
	UInt32 dwProjectID;
};

struct MediaHeader
{
	MediaID id;
	UInt32 uOffset;
	UInt32 uSize;
};

struct Sound
{
	std::string id;
	std::string name;
	bool streamed;
};

char *data;
std::string path;
std::vector<MediaHeader> media;
std::vector<Sound> sounds;

void LoadBank(std::string fname)
{
	FILE *f = fopen(fname.c_str(), "rb");
	SubchunkHeader sc;
	fread(&sc, sizeof(sc), 1, f);
	if (sc.dwTag == BankHeaderChunkID)
	{
		BankHeader h;
		fread(&h, sizeof(h), 1, f);
		fseek(f, sc.dwChunkSize - sizeof(h), SEEK_CUR);
		while (fread(&sc, sizeof(sc), 1, f))
		{
			switch (sc.dwTag)
			{
			case BankDataIndexChunkID:
				for (unsigned int i = 0; i < sc.dwChunkSize / sizeof(MediaHeader); i++)
				{
					MediaHeader m;
					fread(&m, sizeof(m), 1, f);
					media.push_back(m);
				}
				break;
			case BankDataChunkID:
				data = new char[sc.dwChunkSize];
				fread(data, sizeof(data[0]), sc.dwChunkSize, f);
				break;
			default:
				fseek(f, sc.dwChunkSize, SEEK_CUR);
				break;
			}
		}
	}
	fclose(f);
}

void ParseFiles(tinyxml2::XMLNode *xml, bool streamed)
{
	for (xml = xml->FirstChildElement("File");xml;xml = xml->NextSiblingElement("File"))
	{
		Sound sound;
		sound.streamed = streamed;
		sound.id = xml->ToElement()->Attribute("Id");
		xml = xml->FirstChildElement("ShortName");
		sound.name = xml->FirstChild()->ToText()->Value();
		sound.name.erase(sound.name.rfind('.'));
		xml = xml->Parent();
		sounds.push_back(sound);
	}
}

BOOL CALLBACK DlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM /* lParam */)
{
	switch (Message)
	{
	case WM_CLOSE:
		EndDialog(hwnd, 0);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_OPEN:
		{
			char lBuf[MAX_PATH] = "";
			OPENFILENAME of;
			memset(&of, 0, sizeof(OPENFILENAME));
			of.lStructSize = sizeof(OPENFILENAME);
			of.hwndOwner = hwnd;
			of.hInstance = nullptr;
			of.lpstrFilter = "XML files\0*.xml\0\0";
			of.lpstrCustomFilter = nullptr;
			of.nMaxCustFilter = 0;
			of.nFilterIndex = 0;
			of.lpstrFile = lBuf;
			of.nMaxFile = MAX_PATH;
			of.lpstrFileTitle = nullptr;
			of.nMaxFileTitle = 0;
			of.lpstrInitialDir = nullptr;
			of.lpstrTitle = nullptr;
			of.Flags = OFN_FILEMUSTEXIST;
			of.nFileOffset = 0;
			of.nFileExtension = 0;
			of.lpstrDefExt = nullptr;
			of.lCustData = 0;
			of.lpfnHook = nullptr;
			of.lpTemplateName = nullptr;
			if (GetOpenFileName(&of))
			{
				path = lBuf;
				path.erase(path.rfind('\\'));
				std::string bank = lBuf;
				bank.erase(bank.rfind('.'));
				bank += ".bnk";
				tinyxml2::XMLDocument doc;
				tinyxml2::XMLNode *xml = &doc;
				doc.LoadFile(lBuf);
				xml = xml->FirstChildElement("SoundBanksInfo");
				if (!xml)
				{
					return false;
				}
				xml = xml->FirstChildElement("SoundBanks");
				if (!xml)
				{
					return false;
				}
				xml = xml->FirstChildElement("SoundBank");
				if (!xml)
				{
					return false;
				}
				bool found = false;
				xml = xml->FirstChildElement("ReferencedStreamedFiles");
				if (xml)
				{
					ParseFiles(xml, true);
					sounds.erase(std::remove_if(sounds.begin(), sounds.end(), [](const Sound & s)
					{
						std::string fname = path;
						fname += L'\\';
						fname += s.id;
						fname += ".wem";
						struct _stat buf;
						return _stat(fname.c_str(), &buf) == -1;
					}
					), sounds.end());
					found = true;
				}
				xml = xml->Parent();
				xml = xml->FirstChildElement("IncludedMemoryFiles");
				if (xml)
				{
					ParseFiles(xml, false);
					found = true;
				}
				std::sort(sounds.begin(), sounds.end(), [](Sound s1, Sound s2)
				{
					return s1.name < s2.name;
				});
				if (!found)
				{
					return false;
				}
				HWND list = GetDlgItem(hwnd, IDC_SOUNDS);
				LVCOLUMN column;
				column.mask = LVCF_TEXT | LVCF_FMT;
				column.pszText = (char *)"Sound";
				column.fmt = LVCFMT_LEFT;
				ListView_InsertColumn(list, 0, &column);
				for (auto &x : sounds)
				{
					LVITEM item;
					memset(&item, 0, sizeof(item));
					item.mask = LVIF_TEXT;
					item.iItem = 0xFFF;
					item.pszText = (LPSTR)x.name.c_str();
					ListView_InsertItem(list, &item);
				}
				ListView_SetColumnWidth(list, 0, LVSCW_AUTOSIZE);
				LoadBank(bank);
			}
		}
		case IDC_EXTRACT:
		{
			HWND list = GetDlgItem(hwnd, IDC_SOUNDS);
			int item = ListView_GetNextItem(list, -1, LVNI_SELECTED);
			if (item != -1)
			{
				std::string sfname = sounds[item].name;
				sfname += ".wem";
				char lBuf[MAX_PATH] = "";
				strcpy(lBuf, sfname.c_str());
				OPENFILENAME of;
				memset(&of, 0, sizeof(OPENFILENAME));
				of.lStructSize = sizeof(OPENFILENAME);
				of.hwndOwner = hwnd;
				of.hInstance = nullptr;
				of.lpstrFilter = "WEM files\0*.wem\0\0";
				of.lpstrCustomFilter = nullptr;
				of.nMaxCustFilter = 0;
				of.nFilterIndex = 0;
				of.lpstrFile = lBuf;
				of.nMaxFile = MAX_PATH;
				of.lpstrFileTitle = nullptr;
				of.nMaxFileTitle = 0;
				of.lpstrInitialDir = nullptr;
				of.lpstrTitle = nullptr;
				of.Flags = OFN_OVERWRITEPROMPT;
				of.nFileOffset = 0;
				of.nFileExtension = 0;
				of.lpstrDefExt = nullptr;
				of.lCustData = 0;
				of.lpfnHook = nullptr;
				of.lpTemplateName = nullptr;
				if (GetSaveFileName(&of))
				{
					std::string fname = lBuf;
					char *outdata = nullptr;
					long size = 0;
					if (sounds[item].streamed)
					{
						std::string infname = path;
						infname += '\\';
						infname += sounds[item].id;
						infname += ".wem";
						FILE *infile = fopen(infname.c_str(), "rb");
						fseek(infile, 0, SEEK_END);
						size = ftell(infile);
						fseek(infile, 0, SEEK_SET);
						outdata = new char[size];
						fread(outdata, sizeof(outdata[0]), size, infile);
						fclose(infile);
					}
					else
					{
						MediaID id = stoul(sounds[item].id);
						for (unsigned int i = 0; i < media.size(); i++)
						{
							if (media[i].id == id)
							{
								size = media[i].uSize;
								outdata = new char[size];
								memcpy(outdata, &data[media[i].uOffset], size);
								break;
							}
						}
					}
					if (size)
					{
						FILE *outfile = fopen(fname.c_str(), "wb");
						fwrite(outdata, sizeof(outdata[0]), size, outfile);
						fclose(outfile);
					}
				}
			}
		}
		break;
		}
		break;
	default:
		return false;
	}
	return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /* hPrevInstance */,
	LPSTR /* lpCmdLine */, int /* nCmdShow */)
{
	return DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), nullptr, DlgProc);
}
