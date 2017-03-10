#!/usr/bin/env python3

import argparse
import csv
import os
import shutil
from tqdm import tqdm

try: import setGPU
except ImportError: pass

import torch
import torch.nn as nn
import torch.optim as optim
from torch.autograd import Variable

import numpy as np
import numpy.random as npr

import sys

import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
plt.style.use('bmh')

import setproctitle

import model

import sys
from IPython.core import ultratb
sys.excepthook = ultratb.FormattedTB(mode='Verbose',
	 color_scheme='Linux', call_pdb=1)

def print_header(msg):
	print('===>', msg)

def main():	
	parser = argparse.ArgumentParser()	
	parser.add_argument('--no-cuda', action='store_true')
	parser.add_argument('--batchSz', type=int, default=150)
	parser.add_argument('--bn', action='store_true')
	parser.add_argument('--eps', type=float, default=1e-4)
	parser.add_argument('--batchSize', type=int, default=1)
	parser.add_argument('--data', type=str,	default='data')
	parser.add_argument('--gpu', type=int,	default=0)
	parser.add_argument('--noutputs', type=int,	default=3)
	parser.add_argument('--display', type=int,	default=1)
	parser.add_argument('--verbose', type=bool,	default=True)
	parser.add_argument('--maxIter', type=int,	default=20)
	parser.add_argument('--silent', type=bool,	default=True)
	parser.add_argument('--useVicon', type=bool,	default=True)
	parser.add_argument('--squash', type=bool,default= True,)
	parser.add_argument('--model', type=str,default= 'lstm',)
	parser.add_argument('--real_time_net', type=bool,default=True, help='use real-time network approximator')
	# parser.add_argument('--hiddenSize' type=str,default= {9, 6, 6})
	parser.add_argument('--seed', type=int,default=123,)
	parser.add_argument('--hiddenSize', type=list, nargs='+', default="9 6 6")
	# parser.add_argument('--backend', type=str,default='cunn',help='cudnn or cunn')
	# parser.add_argument('--checkpoint', type=str,default='../farnn/src/network/data_fastlstm-net.t7')
	args = parser.parse_args()
	# args.cuda = not args.no_cuda and torch.cuda.is_available()

	nFeatures, nCls, bn = 6, 3, args.bn
	print('bn: ', bn)
	#ineq constraints are [12 x 3] in total
	# print('model ', model)
	net = model.LSTMModel(nFeatures, nCls, bn)
	print(net)
	
if __name__ == '__main__':
	main()