import os.path
import shutil
import glob

PKG_DIR = 'LipschitzPruning'

if os.path.exists(PKG_DIR):
    shutil.rmtree(PKG_DIR)

os.mkdir(f'{PKG_DIR}')
os.mkdir(f'{PKG_DIR}/bin')
os.mkdir(f'{PKG_DIR}/scenes')

for spv_path in glob.glob('*.spv'):
    print(spv_path)
    shutil.copyfile(spv_path, f'{PKG_DIR}/bin/{spv_path}')

shutil.copyfile('Release/LipschitzPruning.exe', f'{PKG_DIR}/bin/LipschitzPruning.exe')

for scene_path in glob.glob('*.json', root_dir='../scenes'):
    print(scene_path)
    shutil.copyfile('../scenes/' + scene_path, f'{PKG_DIR}/scenes/{scene_path}')

shutil.make_archive('LipschitzPruning', 'zip', PKG_DIR)