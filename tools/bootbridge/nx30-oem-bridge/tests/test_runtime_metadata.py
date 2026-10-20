"""Private build provenance must not become firmware runtime metadata."""
import copy
import hashlib
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import build

class RuntimeMetadataTests(unittest.TestCase):
    def setUp(self):
        self.payload=(b'code-profile-test'*100000)[:build.APPSBL_BYTES]
        self.report={'payload_version':'offline-code-v1','base_commit':'a'*40,'derivative_commit':'b'*40,
            'nand_catalog_sha256':'c'*64,'ip':'192.168.10.1','ecc_bits':4,'appsbl_load_ecc_bits':4,'appsbl_storage_ecc_bits':4,
            'release_identity':{'scope':'LAB-CODE-PROFILE','sequence':1,'candidate_sha256':hashlib.sha256(self.payload).hexdigest(),'candidate_bytes':build.APPSBL_BYTES},
            'known_releases':[{'scope':'LAB-CODE-PROFILE','sequence':0,'sha256':next(iter(build.REVIEWED_OEM_APPSBL)),'bytes':build.APPSBL_BYTES,'kind':'oem_baseline'}],
            'sample':'SYNTHETIC-PRIVATE-SAMPLE','serial':'SYNTHETIC-PRIVATE-SN','ethaddr':'SYNTHETIC-MAC',
            'nand_catalog_header_path':'/private/build/location/catalog.h','source_directory':'/private/source/tree',
            'source_provenance':'/private/build/provenance.json','nested_private':{'identity':'SYNTHETIC-PRIVATE-IDENTITY'}}
    def test_only_public_identity_is_embedded(self):
        import json
        public=build.runtime_payload_metadata(self.payload,self.report)
        encoded=json.dumps(public)
        self.assertNotIn('SYNTHETIC-PRIVATE',encoded)
        self.assertNotIn('/private/',encoded)
        self.assertNotIn('ethaddr',encoded)
        self.assertEqual(public['partition_sha256'],hashlib.sha256(self.payload).hexdigest())
        self.assertEqual(public['source_commits']['derivative_commit'],'b'*40)
        self.assertIs(public['install_available'],False)
        changed=copy.deepcopy(self.report);changed['nested_private']={'arbitrary':'another path'};changed['install_available']=True
        self.assertEqual(public,build.runtime_payload_metadata(self.payload,changed))
    def test_binding_format_and_source_errors_are_rejected(self):
        changes=[('base_commit','bad'),('payload_version','bad version'),('nand_catalog_sha256','0'*64),('ip','192.168.1.1'),('ecc_bits',4.0),('appsbl_load_ecc_bits',True)]
        for key,value in changes:
            bad=copy.deepcopy(self.report);bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):build.runtime_payload_metadata(self.payload,bad)
        bad=copy.deepcopy(self.report);bad['release_identity']['candidate_sha256']='f'*64
        with self.assertRaises(ValueError):build.runtime_payload_metadata(self.payload,bad)
        with self.assertRaises(ValueError):build.runtime_payload_metadata(self.payload[:-1],self.report)

if __name__=='__main__':unittest.main()
