/* ************************************************************************* *
 *   This file is part of the `DataStructures` package.                      *
 *                                                                           *
 *   Copyright 2015 Maciej Bartoszuk, Marek Gagolewski,                      *
 *                                                                           *
 *   'DataStructures' is free software: you can redistribute it and/or       *
 *   modify it under the terms of the GNU Lesser General Public License      *
 *   as published by the Free Software Foundation, either version 3          *
 *   of the License, or (at your option) any later version.                  *
 *                                                                           *
 *   'DataStructures' is distributed in the hope that it will be useful,     *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
 *   GNU Lesser General Public License for more details.                     *
 *                                                                           *
 *   You should have received a copy of the GNU Lesser General Public        *
 *   License along with 'DataStructures'.                                    *
 *   If not, see <http://www.gnu.org/licenses/>.                             *
 * ************************************************************************* */


#include "vptree.h"

using namespace Rcpp;
using namespace DataStructures;


VpTree::VpTree(Distance* dist) :
      _root(NULL),
      _n(dist->getObjectCount()),
      _distance(dist),
      _indices(dist->getObjectCount()),
      _indicesinv(dist->getObjectCount()),
      distances(_n)
{
   // starting _indices: random permutation of {0,1,...,_n-1}
   for (size_t i=0;i<_n;i++)
      _indices[i] = i;
   for (size_t i=_n-1; i>= 1; i--)
      swap(_indices[i], _indices[(size_t)(unif_rand()*(i+1))]);

   _root = buildFromPoints(0, _n);
   for (size_t i=0; i<_n; ++i)
      _indicesinv[_indices[i]] = i;
}


VpTree::~VpTree() {
   if (_root) delete _root;
}


size_t VpTree::chooseNewVantagePoint(size_t left, size_t right)
{
   // return random index
   // don'use left one (even if sample seems to be randomized already,
   // vp in subtrees is already on the left...)
   return left+(size_t)(unif_rand()*(right-left));
}


VpTreeNode* VpTree::buildFromPoints(size_t left,
   size_t right)
{
   if (right - left <= /*opts.maxLeavesElems*/4)
   {
      VpTreeNode* leaf = new VpTreeNode(left, right);
      return leaf;
   }

   size_t vpi_idx = chooseNewVantagePoint(left, right);
   std::swap(_indices[left], _indices[vpi_idx]);
   size_t vpi = _indices[left];
   size_t median = (right + left) / 2;

   for (size_t i=left+1; i<right; ++i)
      distances[_indices[i]] = (*_distance)(vpi, _indices[i]);

   std::nth_element(_indices.begin()+left+1, _indices.begin() + median, _indices.begin()+right, DistanceComparatorCached(&distances));

   VpTreeNode* node = new VpTreeNode(vpi, left, left+1, distances[_indices[median]]);
   if (median - left > 0) { // don't include vpi
      node->childL = buildFromPoints(left+1, median+1);
   }
   if (right - median - 1 > 0) {
      node->childR = buildFromPoints(median+1, right);
   }
   return node;
}


void VpTree::getNearestNeighborsFromMinRadiusRecursive(
   VpTreeNode* node, size_t index, double minR, double& maxR,
   std::priority_queue<HeapNeighborItem>& nnheap, size_t maxNNPrefetch)
{
   // search within (minR, maxR]
   // if (node == NULL) return; // this should not happen

   if (node->vpindex == SIZE_MAX) { // leaf
      for (size_t i=node->left; i<node->right; ++i) {
         double dist2 = (*_distance)(_indices[index], _indices[i]); // the slow part
         if (dist2 > maxR || dist2 <= minR) continue;
         if (nnheap.size() >= maxNNPrefetch && dist2 < maxR) {
            while (!nnheap.empty() && nnheap.top().dist == maxR) {
               nnheap.pop();
            }
         }
         nnheap.push( HeapNeighborItem(i, dist2) );
         if (nnheap.size() >= maxNNPrefetch) maxR = nnheap.top().dist;
      }
      return; // nothing more to do
   }
   // else // not a leaf

   // first visit the vantage point
   double dist = (*_distance)(_indices[index], _indices[node->left]); // the slow part
   if (dist <= maxR && dist > minR) {
      if (nnheap.size() >= maxNNPrefetch && dist < maxR) {
         while (!nnheap.empty() && nnheap.top().dist == maxR) {
            nnheap.pop();
         }
      }
      nnheap.push( HeapNeighborItem(node->left, dist) );
      if (nnheap.size() >= maxNNPrefetch) maxR = nnheap.top().dist;
   }

   if (dist < node->radius) {
      if (dist - maxR <= node->radius && dist + node->radius > minR) {
         if (node->childL)
            getNearestNeighborsFromMinRadiusRecursive(node->childL, index, minR, maxR, nnheap, maxNNPrefetch);
      }

      if (dist + maxR >= node->radius) {
         if (node->childR)
            getNearestNeighborsFromMinRadiusRecursive(node->childR, index, minR, maxR, nnheap, maxNNPrefetch);
      }
   }
   else /* ( dist >= node->radius ) */ {
      if (dist + maxR >= node->radius) {
         if (node->childR)
            getNearestNeighborsFromMinRadiusRecursive(node->childR, index, minR, maxR, nnheap, maxNNPrefetch);
      }

      if (dist - maxR <= node->radius && dist + node->radius > minR) {
         if (node->childL)
            getNearestNeighborsFromMinRadiusRecursive(node->childL, index, minR, maxR, nnheap, maxNNPrefetch);
      }
   }
}


vector<HeapNeighborItem> VpTree::getNearestNeighbors(size_t index, int maxNN, double minR, double maxR)
{
   std::priority_queue<HeapNeighborItem> nnheap;
   getNearestNeighborsFromMinRadiusRecursive(_root, _indicesinv[index], minR, maxR, nnheap, maxNN);

   size_t n = nnheap.size();
   vector<HeapNeighborItem> out(n);
   for (size_t i = 0; i<n; ++i) {
      out[n-i-1] = nnheap.top();
      out[n-i-1].index = _indices[out[n-i-1].index];
      nnheap.pop();
   }
   return out;
}
