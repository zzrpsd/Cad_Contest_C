#include "placer.h"
#include <cfloat>
#include <climits>
#include <algorithm>

unsigned Cluster::global_id = 0;

/*
void Placer::place()
{
    plot("init");

    // ------------------------ global placement
    random_place();
    plot("rand");

    if( !gArg.checkExist("nonlp") )
    {
        nlp::NLPlacer gp(*this);
        gp.solve();
        plot("nlp");
    }

    // ------------------------ legalization
    save_modules_2_pos(PL_LAST);
    lga::TetrisT lg(*this);
    lg.legal();
    //lg.legal_original();
  // lg.legal_improved();

    plot("lg");

    rowpl::RowPlacer rp( _cir );
    if( rp.initRowStructures( false ) ){
        cout << "==================================================" << endl;
        cout << "[Congratulation!!] All modules are non-overlapped!" << endl;
        cout << "==================================================" << endl;
    }else{
        cout << "==================================================" << endl;
        cout << "[Legalization Fail] Some modules overlap!!" << endl;
        cout << "(You may see the " << Plotter::convert2pltName( "lg" ) << " for reference.)" << endl;
        cout << "==================================================" << endl;
        getchar();
    }

    // ------------------------ detailed placement
//    dpT::Test t;
//    t.solve();
}
*/

//used in sorting cells
bool pair_compare(pair<int,double>& _p1, pair<int,double>& _p2)
{
    return (_p1.second<_p2.second);
}

double Placer::compute_hpwl()
{
    double totalHPWL = 0;
    for( size_t i=0; i<_cir->numNets(); ++i ){
        Net &tNet = _cir->net(i);
        double lx=DBL_MAX, ly=DBL_MAX, ux=-DBL_MAX, uy=-DBL_MAX;
        for( size_t j=0; j<tNet.numPins(); ++j ){
            Pin &tPin = tNet.pin(j);
            lx=min( tPin.x(), lx );
            ly=min( tPin.y(), ly );
            ux=max( tPin.x(), ux );
            uy=max( tPin.y(), uy );
        }
        totalHPWL += (ux-lx) + (uy-ly);
    }
    return totalHPWL;
}

double Placer::compute_displacement(const Placer::PL_TYPE &pt)
{
    double totalDisp = 0;
    for( size_t i=0; i<_cir->numComponents(); ++i ){
        Module &mod = _cir->module(i);
        totalDisp += Point::dist( mod.pos(), mod_2_pos(mod, pt) );
    }
    return totalDisp;
}

bool Placer::move_module_2_pos(Module &mod, const Point &pos, MOVE_TYPE mt)
{
    if( mod.isFixed() ) return false;

    Point targetPos( pos.x(), pos.y() );

    if( is_equal( pos.x(), mod.x() ) && is_equal( pos.y(), mod.y() ) ) { return true; }

    if( mt == MOVE_ONSITE ){
        _cir->set_x_y_on_site( targetPos );
        //Row &row = _cir->row( _cir->y_2_row_id( targetPos.y() ) );
        assert(_cir->isRowBottomVss(_cir->y_2_row_id( targetPos.y() )) == mod.isBottomVss());
        mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() );//row.orient() != cell's orient
    }
    else
    {
        if(mod.isBottomVss() == _cir->isRowBottomVss( _cir->y_2_row_id( targetPos.y() )))
        {   mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() ); }
        else
        {
            assert(mod.orient() == OR_N || mod.orient() == OR_S);
            Orient _orient = ((mod.orient()==OR_N)?OR_S:OR_N);
            mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , _orient);
            /*
            switch(mod.orient()){
                case OR_N:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , OR_S);
                     break;
                case OR_W:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , OR_E);
                     break;
                case OR_S:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , OR_N);
                     break;
                case OR_E:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , OR_W);
                     break;
                case OR_FN:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , OR_FS);
                     break;
                case OR_FW:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , OR_FE);
                     break;
                case OR_FS:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , OR_FN);  
                     break;
                case OR_FE:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() , OR_FW);
                     break;
                default:
                     mod.setPosition( targetPos.x(), targetPos.y(), _cir->chipRect() );
                     break;
            }
            */
            // still thinking a better way to deal with rotation
            // FN -> FS & FN -> S , which one is better ?
            // Is it possible that VSS is on the EAST or WEST side ?
            // Since we are just dealing with N and S right now, use simpler ((mod.orient()==OR_N)?OR_S:OR_N) 
        }
        mod.setIsBottomVss();
    }

    return true;
}

bool Placer::move_module_center_2_pos(Module &mod, const Point &pos, MOVE_TYPE mt)
{
    return move_module_2_pos( mod, Point( pos.x()-mod.width()/2, pos.y()-mod.height()/2 ), mt );
}

void Placer::save_modules_2_pos(const Placer::PL_TYPE &pl)
{
    for( size_t i=0; i<_cir->numModules(); ++i ){
        _modPLPos[pl][i].set_x_y( _cir->module(i).x(), _cir->module(i).y() );
    }
}

void Placer::bound_pos_in_max_disp(Module &mod, Point &pos, bool isOnSite, const double &dispRatio)
{
    if( param.maxDisp <= 0 ) return;

    if( isOnSite ) _cir->set_x_y_on_site(pos);

    double initX = mod_2_pos( mod, PL_INIT ).x(),
           initY = mod_2_pos( mod, PL_INIT ).y();
    double xDiff = pos.x()-initX,
           yDiff = pos.y()-initY;
    double displacement = fabs( xDiff ) + fabs( yDiff );

    double marginRatio = 0.9;
    bool isFirst = true;
    while( !is_pos_in_max_disp( mod, pos, dispRatio ) )
    {
        if( isFirst )
        {
            xDiff *= param.maxDisp*dispRatio*marginRatio / displacement;
            yDiff *= param.maxDisp*dispRatio*marginRatio / displacement;
            isFirst = false;
        }
        else
        {
            xDiff *= marginRatio;
            yDiff *= marginRatio;
        }

        displacement = fabs( xDiff ) + fabs( yDiff );
        pos.set_x_y( initX+xDiff, initY+yDiff );

        if( isOnSite ) _cir->set_x_y_on_site(pos);

        //cout << "exceed maxDisp" << endl;
    }

    assert( is_pos_in_max_disp( mod, pos ) );
}

bool Placer::is_pos_in_max_disp(Module &mod, const Point &pos, const double &dispRatio)
{
    if( param.maxDisp <= 0 ) return true; // unassigned
    return Point::dist( pos, mod_2_pos(mod, PL_INIT) ) <= param.maxDisp*dispRatio;
}

void Placer::move_pl_pos(const Point &shift, const double &scale, bool isBack)
{
    for( size_t pl=0; pl<3; ++pl ){
        for( size_t i=0; i<_cir->numModules(); ++i ){
            Point &p = _modPLPos[pl][i];
            p.shift_scale( shift, scale, isBack );
        }
    }
    _cir->move_placement( shift, scale, isBack );
}


///////////////////////////////////////////////////////////////
//                Newly Added Functions (ICCAD'17)           //
///////////////////////////////////////////////////////////////

//only for even row height cells
//fix some problems
int Placer::find_valid_row(Module &mod)
{
    bool BottomVss = mod.isBottomVss();
    int rowId = _cir->y_2_row_id( mod.y() );
    int rowHeight = (int)(mod.height()/_cir->rowHeight());
    if(_cir->isRowBottomVss(rowId)==BottomVss && rowId <= (int)(_cir->numRows()-rowHeight) && rowId >= 0 ) { return rowId; }
    if(rowId <= 0)
    {
        if(_cir->isRowBottomVss(0)==BottomVss){ return 0; }
        else { return 1; }
    }
    if(rowId >= (int)(_cir->numRows()-rowHeight))
    {
        if(_cir->isRowBottomVss((int)(_cir->numRows()-rowHeight))==BottomVss){ return (int)(_cir->numRows()-rowHeight); }
        else { return (int)(_cir->numRows()-rowHeight-1); }
    }

    if( fabs(_cir->row_id_2_y(rowId-1)-mod.y()) < fabs(_cir->row_id_2_y(rowId+1)-mod.y()))
    {
        return rowId-1;
    }
    return rowId+1;
}

//only for std cell
void Placer::place_valid_site(Module &mod)
{
    assert(mod.isStdCell());
    int rowId;
    Orient orient = mod.orient();
    bool before = mod.isBottomVss(),change = false;
    if((int)(mod.height()/_cir->rowHeight())%2 == 0)
    {
        rowId =find_valid_row(mod);
    }
    else
    {
        rowId = _cir->y_2_row_id( mod.y() );
        if(_cir->isRowBottomVss(rowId) != mod.isBottomVss())
        { 
            orient = ((mod.orient()==OR_N)?OR_S:OR_N); 
            change = true;
        }
    }
    double xPos = _cir->g_x_on_site(mod.x(), rowId, Circuit::ALIGN_HERE);
    double yPos = _cir->row_id_2_y(rowId);
    mod.setPosition( xPos, yPos, _cir->chipRect(), orient );
    mod.setIsBottomVss();
    if(change) { assert(before != mod.isBottomVss()); }
}

void Placer::place_all_mods_to_site()
{
    for(unsigned i = 0 ; i < _cir->numModules() ; i++)
    {
        if(_cir->module(i).isStdCell()) { place_valid_site(_cir->module(i)); }
    }
}

void Placer::sort_cells()
{
    cell_order.clear();
    vector< pair<int,double> > index_x_pair;
    index_x_pair.reserve(_cir->numModules());
    for( unsigned  moduleId = 0 ; moduleId < _cir->numModules() ; moduleId++ )
    {
        index_x_pair.push_back(make_pair(moduleId, _modPLPos[0][moduleId].x() ));
    }

    sort(index_x_pair.begin(), index_x_pair.end(), pair_compare);

    cell_order.reserve(_cir->numModules());
    for( unsigned  moduleId = 0 ; moduleId < _cir->numModules() ; moduleId++ )
    {
        cell_order.push_back(index_x_pair[moduleId].first);
    }
}

void Placer::print_cell_order() const
{
    for( unsigned  moduleId = 0 ; moduleId < _cir->numModules() ; moduleId++ )
    {
        unsigned index = cell_order[moduleId];
        if(!_cir->module(index).isStdCell()){ continue; }
        cout<<"Cell X position = "<<_modPLPos[0][index];
        //cout<<"Cell Name = "<<_cir->module(index).name()<<"; X position = "<<_modPLPos[0][index];
        cout<<"; #Row = "<<(int)(_cir->module(index).height()/_cir->rowHeight())<<endl;
    }
}

/*
void Placer::print_fanins_fanouts(Cluster* _clus) const
{
    cout<<"///// Print Fanins Fanouts of a cluster /////\n";
    for(unsigned i = 0 ; i < _clus->_modules.size() ; i++)
    {
        _clus->_modules[i]->printFI();
        _clus->_modules[i]->printFO();
    }
}
*/

bool Placer::check_all(int to_index) const
{
    int numOfOverlap = 0;
    vector< vector< pair<int,double> > > temp_nodes;
    temp_nodes.resize(_cir->numRows());
    for(int i = 0 ; i <= to_index ; i++)
    {
        int index = cell_order[i];
        if(!_cir->module(index).isStdCell()) continue;
        Cluster* _clus = _cellIdClusterMap[_cir->module(index).dbId()];
        int inclusIndex = _clus->_cellIdModuleMap.find(_cir->module(index).dbId())->second;
        for(int j = 0 ; j < _clus->_modules[inclusIndex]->_degree ; j++)
        {
            temp_nodes[_clus->_modules[inclusIndex]->_rowId+j].push_back(make_pair(index,_clus->_x_ref+_clus->_delta_x[inclusIndex]));
        }
    }

    for(unsigned i = 0 ; i < _cir->numRows() ; i++)
    {
        sort(temp_nodes[i].begin(), temp_nodes[i].end(), pair_compare);
    }

    for(unsigned i = 0 ; i < _cir->numRows() ; i++)
    {
        if(temp_nodes[i].empty()) { continue; }
        for(unsigned j = 0 ; j < temp_nodes[i].size()-1 ; j++)
        {
            int index = temp_nodes[i][j].first;
            int next_index = temp_nodes[i][j+1].first;
            double width = _cir->module(index).width();
            if(temp_nodes[i][j].second+width > temp_nodes[i][j+1].second)
            {
                cout<<"\nOverlap Ocurred in row #"<<i<<endl;
                cout<<"Module : < "<<_cir->module(index).name()<<" > overlaps with ";
                cout<<"module : <  "<<_cir->module(next_index).name()<<" >"<<endl;
                cout<<"First Module position = "<<temp_nodes[i][j].second<<endl;
                cout<<"Second Module position = "<<temp_nodes[i][j+1].second<<endl;
                cout<<"First Module Width = "<<width<<endl;

                cout<<"Module <"<<_cir->module(index).name()<<"> orig position = "<<_modPLPos[0][index].x()<<endl;
                print_delta_x(_cellIdClusterMap[_cir->module(index).dbId()]);

                cout<<"Module <"<<_cir->module(next_index).name()<<"> orig position = "<<_modPLPos[0][next_index].x()<<endl;
                print_delta_x(_cellIdClusterMap[_cir->module(next_index).dbId()]);

                //int prev_id = prev_cells[i].find(_cir->module(index).dbId())->second;
                int prev_id = prev_cells[i][_cir->module(index).dbId()];
                cout<<"But Prev Module : < "<<_cir->module(prev_id).name()<<" >\n";
                Cluster* _prevClus = _cellIdClusterMap[prev_id];
                int inclusIndex = _prevClus->_cellIdModuleMap.find(prev_id)->second;
                cout<<"Prev Module position = "<<_prevClus->_delta_x[inclusIndex]+_prevClus->_x_ref<<endl;
                cout<<"Prev Module Width = "<<_prevClus->_modules[inclusIndex]->_module->width()<<endl;

                numOfOverlap++;
            }
        }
    }

    cout<<"number of overlaps = "<<numOfOverlap<<endl;
    return (numOfOverlap!=0);   //return true if there is overlap
}

void Placer::print_delta_x(Cluster* _clus) const
{
    cout<<"///// Print Delta X of Cluster /////\n";
    cout<<"ref module position = "<<_clus->_x_ref<<endl;
    for(unsigned i = 0 ; i < _clus->_modules.size() ; i++)
    {
        cout<<"Module Name = "<<_clus->_modules[i]->_module->name();
        cout<<"; delta_x = "<<_clus->_delta_x[i];
        cout<<"; rowId = "<<_clus->_modules[i]->_rowId<<"~"<<_clus->_modules[i]->_rowId+_clus->_modules[i]->_degree-1<<endl;
    }
}

void Placer::print_last_module_name() const
{
    cout<<" ///// Print last module in each row ///// \n";
    for(unsigned i = 0 ; i < _cir->numRows() ; i++)
    {
        if(_rowIdClusterMap[i] != 0)
        {
            int _index = (_rowIdClusterMap[i]->_lastNode.find(i))->second;
            Node* _node = _rowIdClusterMap[i]->_modules[_index];
            cout<<"Row #"<<i<<" has last module named = "<<_node->_module->name()<<endl;
        }
    }
}

bool Placer::check_cluster_internal_overlap(Cluster* _clus)
{
    cout<<"Check\n";
    vector< vector< pair<int,double > > > temp_nodes;
    temp_nodes.resize(_cir->numRows());
    bool isOverlap = false;

    for(unsigned i = 0 ; i < _clus->_modules.size() ; i++)
    {
        //cout<<"degree = "<<_clus->_modules[i]->_degree <<endl;
        for(int j = 0 ; j < _clus->_modules[i]->_degree ; j++)
        {
            //cout<<"_clus->_modules[i]->_rowId+j = "<<_clus->_modules[i]->_rowId+j<<endl;
            //assert(_clus->_modules[i]->_rowId+j < _cir->numRows());
            temp_nodes[_clus->_modules[i]->_rowId+j].push_back(make_pair(i,_clus->_delta_x[i]));
        }
    }
    //cout<<"1";

    for(unsigned i = 0 ; i < _cir->numRows() ; i++)
    {
        sort(temp_nodes[i].begin(), temp_nodes[i].end(), pair_compare);
    }
    //cout<<"2";

    for(unsigned i = 0 ; i < _cir->numRows() ; i++)
    {
        if(temp_nodes[i].empty()) { continue; }
        for(unsigned j = 0 ; j < temp_nodes[i].size()-1 ; j++)
        {
            int index = temp_nodes[i][j].first;
            int next_index = temp_nodes[i][j+1].first;
            if(_clus->_delta_x[index]+_clus->_modules[index]->_module->width() > _clus->_delta_x[next_index])
            {
                isOverlap = true;
                cout<<"\nOverlap Ocurred in row #"<<i<<endl;
                cout<<"Module : < "<<_clus->_modules[index]->_module->name()<<" > overlaps with ";
                cout<<"module : <  "<<_clus->_modules[next_index]->_module->name()<<" >"<<endl;
                cout<<"First Module position = "<<_clus->_delta_x[index]+_clus->_x_ref<<endl;
                cout<<"Second Module position = "<<_clus->_delta_x[next_index]+_clus->_x_ref<<endl;
                cout<<"First Module Width = "<<_clus->_modules[index]->_module->width()<<endl;

                cout<<"Module <"<<_clus->_modules[index]->_module->name()<<"> orig position = "<<_modPLPos[0][index].x()<<endl;
                cout<<"Module <"<<_clus->_modules[next_index]->_module->name()<<"> orig position = "<<_modPLPos[0][next_index].x()<<endl;

                int prev_id = prev_cells[i][_clus->_modules[next_index]->_module->dbId()];
                cout<<"But Prev Module : < "<<_cir->module(prev_id).name()<<" >\n";
                Cluster* _prevClus = _cellIdClusterMap[prev_id];
                int inclusIndex = _prevClus->_cellIdModuleMap.find(prev_id)->second;
                cout<<"Prev Module position = "<<_prevClus->_delta_x[inclusIndex]+_prevClus->_x_ref<<endl;
                cout<<"Prev Module Width = "<<_prevClus->_modules[inclusIndex]->_module->width()<<endl;
                //break;
            }
        }
        //if(isOverlap) { break; }
    }
    //cout<<"end\n";

    return isOverlap;
}

//this area is made just to try some functionalities of the code
void Placer::try_area()
{
    cout<<"Number Of modules = "<<_cir->numModules()<<endl;
    //cin.get();
    for(unsigned i = 0 ; i < _cir->numModules() ; i++)
    {
        cout<<"Placing Module number : "<<i<<endl;
        if(!_cir->module(cell_order[i]).isStdCell()) continue;

        Module* _cell = &_cir->module(cell_order[i]);
        int rowHeight = (int)(_cell->height()/_cir->rowHeight());
        int rowNum = _cir->y_2_row_id(_cell->y());
        double cost_best = DBL_MAX;
        int row_best = rowNum;

        /*
        //find previous cluster (with max x position) if one exist
        //upward
        for(int counter = 0 ; counter <= (int)(_cir->numRows()-rowNum-rowHeight ) ; counter++)
        {
            assert(rowNum+counter <= (int)(_cir->numRows()-rowHeight));
            //cout<<"RowNum+counter = "<<rowNum+counter<<endl;
            if(abs(_cell->y()-_cir->row_id_2_y(rowNum+counter)) > cost_best) break;
            if(counter > 15) break;
            if(rowHeight % 2 == 0 && counter % 2 == 1) continue;
            double cost;

            cost = Multi_PlaceRow_trial(_cell,rowHeight,rowNum+counter);
            //cout<<"upward\n";
            if(cost < cost_best){
                cost_best = cost;
                row_best = rowNum+counter;
            }
        }
        //downward
        for(int counter = 1 ; counter <= rowNum ; counter++)
        {
            assert(rowNum-counter >= 0);
            //cout<<"RowNum+counter = "<<rowNum+counter<<endl;
            if(abs(_cell->y()-_cir->row_id_2_y(rowNum+counter)) > cost_best) break;
            if(counter > 15) break;
            if(rowHeight % 2 == 0 && counter % 2 == 1) continue;
            double cost;

            cost = Multi_PlaceRow_trial(_cell,rowHeight,rowNum-counter);
            //cout<<"downward\n";
            if(cost < cost_best){
                cost_best = cost;
                row_best = rowNum-counter;
            }
        }*/
        //cout<<"Done!"<<endl;
        
        Multi_PlaceRow(_cell,rowHeight,row_best);
    }
    //assert(!check_all(_cir->numModules()-1));

    cout<<"Now renew Position\n";
    //cout<<"Cluster number = "<<_clusters.size()<<endl;
    //int counter = 0;
    for(map<int,Cluster*>::iterator iter = _clusters.begin() ; iter != _clusters.end() ; ++iter)
    {
        //cout<<"counter = "<<counter;
        //counter++;
        RenewPosition(*(iter->second));
    }

    cout<<"Cluster number = "<<_clusters.size()<<endl;
}
/*
void Placer::try_area2()
{
    int index = 20480;
    Cluster* _clus = new Cluster();
    Module* _cell = &_cir->module(cell_order[index]);
    cout<<"Cell Name = "<<_cell->name()<<"; X position = "<<_modPLPos[0][cell_order[index]];
    cout<<"; #Row = "<<(int)(_cell->height()/_cir->rowHeight())<<"; width = "<<_cell->width()<<endl;
    int rowId = _cir->y_2_row_id(_cell->y());

    AddCell(_clus,_cell, rowId, true);
    cout<<"Cluster position (_x_ref) = "<<_clus->_x_ref<<endl;
    
    index = 20112;
    _cell = &_cir->module(cell_order[index]);
    cout<<"Cell Name = "<<_cell->name()<<"; X position = "<<_modPLPos[0][cell_order[index]];
    cout<<"; #Row = "<<(int)(_cell->height()/_cir->rowHeight())<<"; width = "<<_cell->width()<<endl;

    AddCell(_clus,_cell, rowId-2, false);
    cout<<"Cluster position (_x_ref) = "<<_clus->_x_ref<<endl;

    index = 20490;
     _cell = &_cir->module(cell_order[index]);
    cout<<"Cell Name = "<<_cell->name()<<"; X position = "<<_modPLPos[0][cell_order[index]];
    cout<<"; #Row = "<<(int)(_cell->height()/_cir->rowHeight())<<"; width = "<<_cell->width()<<endl;

    AddCell(_clus,_cell, rowId-1, false);
    cout<<"Cluster position (_x_ref) = "<<_clus->_x_ref<<endl;

    //cout<<"Printing Fo FIS\n";
    //print_fanins_fanouts(_clus);
    print_delta_x(_clus);

    //////////////////////////////////////////////////////////////////////////////////
    cout<<"/////////////////////////\n";
    index = 20495;
    Cluster* _clus2 = new Cluster();
    _cell = &_cir->module(cell_order[index]);
    cout<<"Cell Name = "<<_cell->name()<<"; X position = "<<_modPLPos[0][cell_order[index]];
    cout<<"; #Row = "<<(int)(_cell->height()/_cir->rowHeight())<<"; width = "<<_cell->width()<<endl;
    //rowId = _cir->y_2_row_id(_cell->y());

    AddCell(_clus2,_cell, rowId-1, true);
    cout<<"Cluster position (_x_ref) = "<<_clus2->_x_ref<<endl;
    
    index = 20500;
    _cell = &_cir->module(cell_order[index]);
    cout<<"Cell Name = "<<_cell->name()<<"; X position = "<<_modPLPos[0][cell_order[index]];
    cout<<"; #Row = "<<(int)(_cell->height()/_cir->rowHeight())<<"; width = "<<_cell->width()<<endl;

    AddCell(_clus2,_cell, rowId-2, false);
    cout<<"Cluster position (_x_ref) = "<<_clus2->_x_ref<<endl;

    index = 20505;
     _cell = &_cir->module(cell_order[index]);
    cout<<"Cell Name = "<<_cell->name()<<"; X position = "<<_modPLPos[0][cell_order[index]];
    cout<<"; #Row = "<<(int)(_cell->height()/_cir->rowHeight())<<"; width = "<<_cell->width()<<endl;

    AddCell(_clus2,_cell, rowId-2, false);
    cout<<"Cluster position (_x_ref) = "<<_clus2->_x_ref<<endl;

    //cout<<"Printing Fo FIS\n";
    //print_fanins_fanouts(_clus2);
    print_delta_x(_clus2);
    ///////////////////////////////////////////////////////

    //Cluster* ret_clus = AddCluster(&_cir->module(cell_order[20490]),&_cir->module(cell_order[20495]));
    Cluster* ret_clus = Collapse(_clus2);
    //print_fanins_fanouts(ret_clus);
    print_delta_x(ret_clus);

    print_last_module_name();

    check_cluster_internal_overlap(ret_clus);
}*/

/////////////////////////////////////////////////
//           Functions for Clusters            //
/////////////////////////////////////////////////

/////////////////TO-DO/////////////
//_firstCell is true if _cell is the first cell of _clus 
//1. create node
//2. find previous node in cluster
//3. renew e, q, delta_x, x_ref, (ref_module)
//4. add module to _modules, _lastNode, _cellIdModuleMap
//5. renew _cellIdClusterMap and _rowIdClusterMap
//This Function only adds cells at the end, and shouldn't add cell in the middle or start of the cluster
void Placer::AddCell(Cluster* _clus, Module* _cell, int _rowNum, bool _firstCell)
{
    //cout<<"Addcell\n";
    assert(_cell->isStdCell()); //assert is standard cell (module includes preplaced blocks, I/O pins)
    int rowHeight = (int)(_cell->height()/_cir->rowHeight());
    Node* _newNode = new Node(_cell, rowHeight, _rowNum);
    _clus->_e += _cell->weight();   //numPins()

    //add module to _modules, _cellIdModuleMap
    _clus->_modules.push_back(_newNode);
    _clus->_cellIdModuleMap[_cell->dbId()] = _clus->_modules.size()-1;

    map<int, int>::iterator _iter;

    if(_firstCell)
    {
        assert(_clus->_modules.size() == 1);
        _clus->_ref_module = _newNode;
        _clus->_delta_x.push_back(0);      // delta_x == 0 if module == ref module
        _clus->_q += (_cell->weight())*(_modPLPos[0][_cell->dbId()].x());    //q <- q + e*(x'(i)-delta_x(i))
        for(int i = 0; i < rowHeight ; i++)   
        {
            Cluster* _prevClus = _rowIdClusterMap[_rowNum+i];  //find previous cluster
            if(_prevClus != 0 )       //if do find a cluster
            {
                assert(_prevClus != _clus);         
                int nodeIndex = (_prevClus->_lastNode.find(_rowNum+i))->second;
                Node* _prevNode = _prevClus->_modules[nodeIndex];
                prev_cells[_rowNum+i][_cell->dbId()] = _prevNode->_module->dbId();
                next_cells[_rowNum+i][_prevNode->_module->dbId()] = _cell->dbId();
            }
        }
    }
    else
    {
        int delta_x = INT_MIN;
        // find previous node in cluster ( set "all"(not just adjacent ones) previous cells to FIs)->already modified 

        // renew prev_cells at the same time
        for(int i = 0; i < rowHeight ; i++)   
        {
            _iter = _clus->_lastNode.find(_rowNum+i);
            if(_iter != _clus->_lastNode.end())    //find previous node
            {
                Node* _prevNode = _clus->_modules[_iter->second];
                //int index = (_rowNum+i)-_prevNode->_rowId;
                //assert(index >=0 && index < _prevNode->_degree && _prevNode->getFO(index) == 0);
                //_prevNode->setFO(index,_newNode);
                //_newNode->setFI(i,_prevNode);
                if(_clus->_delta_x[_iter->second]+_prevNode->_module->width() > delta_x)
                {
                    delta_x = _clus->_delta_x[_iter->second]+_prevNode->_module->width();
                }
                //FI_positions[i] = _clus->_delta_x[_iter->second]+_prevNode->_module->width();
            }

            Cluster* _prevClus = _rowIdClusterMap[_rowNum+i];  //find previous cluster
            if(_prevClus != 0 )       //if do find a cluster
            {
                //assert(_prevClus != _clus);        //cluster will not be _clus (or else will not go in here)
                int nodeIndex = (_prevClus->_lastNode.find(_rowNum+i))->second;
                Node* _prevNode = _prevClus->_modules[nodeIndex];
                prev_cells[_rowNum+i][_cell->dbId()] = _prevNode->_module->dbId();
                next_cells[_rowNum+i][_prevNode->_module->dbId()] = _cell->dbId();
            }

        }
        //eliminate non adjacent cells
        /*
        for(int i = 0 ; i < rowHeight ; i++)
        {
            if(_newNode->getFI(i) != 0 && delta_x-FI_positions[i] > 0.001)
            {
                assert(rowHeight > 1);
                int index = (_rowNum+i)-_newNode->getFI(i)->_rowId;
                _newNode->getFI(i)->setFO(index,0);
                _newNode->setFI(i,0);
            }
        }
        */

        assert(delta_x != INT_MIN);
        // renew _clus->_delta_x
        _clus->_delta_x.push_back(delta_x);

        // renew q
        _clus->_q += (_cell->weight())*(_modPLPos[0][_cell->dbId()].x()-delta_x);
    }

    // renew _clus->_lastnode and _rowIdClusterMap
    for(int i = 0; i < rowHeight ; i++)   
    {
        // renew _clus->_lastnode
        _clus->_lastNode[_rowNum+i] = (_clus->_modules.size()-1);
        //renew _rowIdClusterMap
        _rowIdClusterMap[_rowNum+i] = _clus;
    }

    // renew _cellIdClusterMap
    assert(_cellIdClusterMap[_cell->dbId()] == 0);   //assert not exist
    _cellIdClusterMap[_cell->dbId()] = _clus;

    // renew _x_ref
    set_x_to_site(_clus);

    //cout<<"Add cell done\n";
}

/////////////////TO-DO/////////////
// _prevCell is the cell in previous cluster which overlaps with _cell
// _prevCell and _cell must belongs to different cluster and should overlap
// assert that _prevCell is the cell in _perclus that has the largest x overlap with _clus
// will check and possibly modify _rowIdClusterMap and _cellIdClusterMap
// only renew FI and FO of the input cells (will wrong if there are multiple collisions)
//
Cluster* Placer::AddCluster(Module* _prevCell, Module* _cell)
{
    //cout<<"Add Cluster\n";
    Cluster* _prevClus = _cellIdClusterMap[_prevCell->dbId()];
    Cluster* _clus = _cellIdClusterMap[_cell->dbId()];
    assert(_prevClus != 0 && _clus != 0 && _prevClus != _clus);
    int _prevNodeIndex = _prevClus->_cellIdModuleMap.find(_prevCell->dbId())->second;
    int _nodeIndex = _clus->_cellIdModuleMap.find(_cell->dbId())->second;

    //assert overlap is true
    assert(_prevClus->_x_ref+_prevClus->_delta_x[_prevNodeIndex]+_prevCell->width() > _clus->_x_ref+_clus->_delta_x[_nodeIndex]);

    double ref_dist = _prevClus->_delta_x[_prevNodeIndex]+_prevCell->width()-_clus->_delta_x[_nodeIndex];

    if(_prevClus->_modules.size() >= _clus->_modules.size())    //ref cell belongs to prevClus
    {
        //renew e, q, delta_x, _prevClus->_modules, _cellIdModuleMap
        _prevClus->_e += _clus->_e;
        _prevClus->_q +=_clus->_q - (_clus->_e)*(ref_dist);
        for(unsigned i = 0 ; i < _clus->_modules.size() ; i++)
        {
            _prevClus->_modules.push_back(_clus->_modules[i]);
            _prevClus->_delta_x.push_back(_clus->_delta_x[i]+ref_dist);
            _prevClus->_cellIdModuleMap[_clus->_modules[i]->_module->dbId()] = _prevClus->_modules.size()-1;
        }

        //re-establish _lastNode
        _prevClus->_lastNode.clear();
        vector<pair<int,int> > lastNodeTemp; //first: index in modules, second: delta_x+width
        lastNodeTemp.resize(_cir->numRows(),make_pair(-1,INT_MIN));
        for(unsigned i = 0 ; i < _prevClus->_modules.size() ; i++)
        {
            int rowId = _prevClus->_modules[i]->_rowId;
            for(int j = rowId ; j < rowId+_prevClus->_modules[i]->_degree ; j++)
            {
                if(lastNodeTemp[j].second < _prevClus->_delta_x[i]+_prevClus->_modules[i]->_module->width())
                {
                    lastNodeTemp[j] = make_pair(i,_prevClus->_delta_x[i]+_prevClus->_modules[i]->_module->width()); 
                }
            }
        }
        for(unsigned i = 0 ; i < _cir->numRows() ; i++)
        {
            if(lastNodeTemp[i].second != INT_MIN)   //exist some node
            {
                _prevClus->_lastNode[i] = lastNodeTemp[i].first;
            }
        }

        //check and modify _rowIdClusterMap and _cellIdClusterMap
        for(unsigned i = 0 ; i < _rowIdClusterMap.size() ; i++)
        {
            if(_rowIdClusterMap[i]==_clus) { _rowIdClusterMap[i] = _prevClus; }
        }
        for(unsigned i = 0 ; i < _clus->_modules.size() ; i++)
        {
            _cellIdClusterMap[_clus->_modules[i]->_module->dbId()] = _prevClus;    
        }

        //delete clus
        _clusters.erase(_clus->id);
        delete _clus;

        //re-evaluate position
        set_x_to_site(_prevClus);

        return _prevClus;
    }
    else    //ref cell belongs to clus
    {
        //renew e, q, delta_x, _prevClus->_modules, _cellIdModuleMap
        _clus->_e += _prevClus->_e;
        _clus->_q += _prevClus->_q + (_prevClus->_e)*(ref_dist);
        for(unsigned i = 0 ; i < _prevClus->_modules.size() ; i++)
        {
            _clus->_modules.push_back(_prevClus->_modules[i]);
            _clus->_delta_x.push_back(_prevClus->_delta_x[i]-ref_dist);
            _clus->_cellIdModuleMap[_prevClus->_modules[i]->_module->dbId()] = _clus->_modules.size()-1;
        }

        //re-establish _lastNode
        _clus->_lastNode.clear();
        vector<pair<int,int> > lastNodeTemp; //first: index in modules, second: delta_x+width
        lastNodeTemp.resize(_cir->numRows(),make_pair(-1,INT_MIN));
        for(unsigned i = 0 ; i < _clus->_modules.size() ; i++)
        {
            int rowId = _clus->_modules[i]->_rowId;
            for(int j = rowId ; j < rowId+_clus->_modules[i]->_degree ; j++)
            {
                if(lastNodeTemp[j].second < _clus->_delta_x[i]+_clus->_modules[i]->_module->width())
                {
                    lastNodeTemp[j] = make_pair(i,_clus->_delta_x[i]+_clus->_modules[i]->_module->width()); 
                }
            }
        }
        for(unsigned i = 0 ; i < _cir->numRows() ; i++)
        {
            if(lastNodeTemp[i].second != INT_MIN)   //exist some node
            {
                _clus->_lastNode[i] = lastNodeTemp[i].first;
            }
        }

        //check and modify _rowIdClusterMap and _cellIdClusterMap
        for(unsigned i = 0 ; i < _rowIdClusterMap.size() ; i++)
        {
            if(_rowIdClusterMap[i]==_prevClus) { _rowIdClusterMap[i] = _clus; }
        }
        for(unsigned i = 0 ; i < _prevClus->_modules.size() ; i++)
        {
            _cellIdClusterMap[_prevClus->_modules[i]->_module->dbId()] = _clus;    
        }

        //delete prevClus
        _clusters.erase(_prevClus->id);
        delete _prevClus;

        //re-evaluate position
        set_x_to_site(_clus);

        return _clus;
    }
}

void Placer::Decluster()
{

}

//try putting a "single" row height cell in the white space of _rowNum
//put the target cell in the first placeable dead space (i.e. first area that can accommodate the cell)
//return true if a dead space exist and put the cell in it
bool Placer::reduce_DeadSpace(Module* _cell, int _rowNum)
{
    assert(_cell->isStdCell()); //assert is standard cell (module includes preplaced blocks, I/O pins)
    assert((int)(_cell->height()/_cir->rowHeight()) == 1);
    bool find_deadSpace = false;

    if(_rowIdClusterMap[_rowNum] == 0) { return false; }
    Cluster* _lastClus = _rowIdClusterMap[_rowNum];
    int index = (_lastClus->_lastNode.find(_rowNum))->second;
    int last_cell_id = _lastClus->_modules[index]->_module->dbId();

    int last_cell_left_x = _lastClus->_x_ref + _lastClus->_delta_x[_lastClus->_cellIdModuleMap.find(last_cell_id)->second];

    //search the dead sapce cells by cells from the last cell in _rowNum
    while(!find_deadSpace)
    {
        break;
    }
    return true;
}

void Placer::RenewPosition(Cluster &c1) 
{
    //cout<<"renew position cluster : "<<c1.id<<endl;
    
    for(size_t i = 0 ; i < c1._modules.size() ; i++){
        Point pos(c1._x_ref+c1._delta_x[i],_cir->row_id_2_y(c1._modules[i]->_rowId));
        //if(c1._modules[i]->_degree%2)
          //  move_module_2_pos(*c1._modules[i]->_module,pos,MOVE_ONSITE);
        //else
            move_module_2_pos(*c1._modules[i]->_module,pos,MOVE_FREE); // not finished, need to check
    }
}

double Placer::RenewCost(Cluster &c1)   
{
    double _cost = 0; // already had a data member "cost" in class cluster, we can just store cost in there
                   // need to return new cost & compare, so I can't store in c1._cost, right? 
    for(size_t i = 0 ; i < c1._modules.size() ; i++){
        _cost += abs(c1._x_ref+c1._delta_x[i]-_modPLPos[0][c1._modules[i]->_module->dbId()].x()); 
        _cost += abs(_cir->row_id_2_y(c1._modules[i]->_rowId)-_modPLPos[0][c1._modules[i]->_module->dbId()].y());
        // how about y ? where should we store the temporary y of all cells ? ans: in node's rowId
    }
    return _cost;
}

Cluster* Placer::Collapse(Cluster* _clus, bool check)
{
    //cout<<"Collapsing: ";
    pair<int,int> _overlap = CheckOverlap(_clus);
    //cout<<"CheckOverlap done\n";
    if(get<0>(_overlap)!=0 || get<1>(_overlap)!=0){
        _clus = AddCluster(&_cir->module(get<0>(_overlap)),&_cir->module(get<1>(_overlap)));
        if(check){ assert(!check_cluster_internal_overlap(_clus)); }
        _clus = Collapse(_clus,check);
    }
    return _clus;
}

pair<int,int> Placer::CheckOverlap(Cluster* _clus)
{
    pair<int,int> overlap;
    overlap = make_pair(0,0);
    int _x_max = 0;
    //map<int,int>::iterator iter ;

    for(unsigned i = 0 ; i < _clus->_modules.size() ; i++){
        int ref = _clus->_modules[i]->_module->dbId(); //overlap cell id in _clus
        int ref_prev = -1;                             //overlap cell id in prev clus
        int row = _clus->_modules[i]->_rowId;
        for(int j = row ; j < row + _clus->_modules[i]->_degree ; j++){
            //iter = prev_cells[j].find(ref);
            //if(iter == prev_cells[j].end()) { continue; }
            //ref_prev = iter->second;    // if this cell is the first cell in the row ? ans: then find will return map::end()
            if(prev_cells[j][ref] == -1) { continue; }
            ref_prev = prev_cells[j][ref];
            if(_cellIdClusterMap[ref_prev]->id != _clus->id){
                int x_ref = _cellIdClusterMap[ref_prev]->_x_ref;
                x_ref += _cellIdClusterMap[ref_prev]->_delta_x[_cellIdClusterMap[ref_prev]->_cellIdModuleMap.find(ref_prev)->second];
                x_ref += _cir->module(ref_prev).width();

                //int x_ref_next = _clus->_delta_x[_clus->_cellIdModuleMap.find(ref_next)->second] + _clus->_x_ref;
                int x_ref_next = _clus->_delta_x[i] + _clus->_x_ref;

                if(x_ref_next < x_ref && (x_ref-x_ref_next) > _x_max){
                    _x_max = x_ref-x_ref_next;
                    overlap = make_pair(ref_prev,ref);
                }
            }
        }
    }
    if(_x_max != 0){/*cout<<"Overlap = "<<_x_max<<endl;*/}
    return overlap; // return (0,0) if no overlap occurs
}

void Placer::set_x_to_site(Cluster* _clus)
{
    double x = (_clus->_q/_clus->_e);
    _clus->_x_ref = _cir->g_x_on_site(x, 0, Circuit::ALIGN_HERE);
}

double Placer::Multi_PlaceRow(Module* _cell, int rowHeight, int rowNum)
{
    /*
    _cell->setIsBottomVss();
    if(rowHeight%2 == 0){
        if(_cell->isBottomVss() != _cir->isRowBottomVss(rowNum)) return INT_MAX;
    }
    */
    // setIsBottomVss() is useful only after the cell was flipped
    if(rowHeight%2 == 0 && _cell->isBottomVss() != _cir->isRowBottomVss(rowNum)) 
    { 
        assert(false); 
    }

    Cluster* _cluster = 0;
    int maxX = INT_MIN;
    for(int j = 0 ; j < rowHeight ; j++ )
    {
        if(_rowIdClusterMap[rowNum+j] == 0) continue;
        Cluster* _lastClus = _rowIdClusterMap[rowNum+j];
        int index = (_lastClus->_lastNode.find(rowNum+j))->second;
        int rightmost_x = _lastClus->_x_ref + _lastClus->_delta_x[index] + _lastClus->_modules[index]->_module->width();
        if(rightmost_x > maxX ) 
        {
            maxX = rightmost_x;
            if(rightmost_x > (_modPLPos[0][_cell->dbId()].x()))
            {
                _cluster = _lastClus;
            }
        }
    }
    if(_cluster == 0) // new cluster
    {
        _cluster = new Cluster();
        _clusters[_cluster->id] = _cluster;
        AddCell(_cluster, _cell, rowNum, true);
        _cluster->_cost = RenewCost(*_cluster);
    }
    else              // add to _cluster
    {
        AddCell(_cluster, _cell, rowNum, false);
        _cluster = Collapse(_cluster);//,i==18363);
        _cluster->_cost = RenewCost(*_cluster);
    }
    return 0;
}

double Placer::Multi_PlaceRow_trial(Module* _cell, int rowHeight, int rowNum)
{
    if(rowHeight%2 == 0 && _cell->isBottomVss() != _cir->isRowBottomVss(rowNum)) 
    { 
        assert(false); 
    }
    Cluster* _cluster = 0;
    int maxX = INT_MIN;
    for(int j = 0 ; j < rowHeight ; j++ )
    {
        if(_rowIdClusterMap[rowNum+j] == 0) continue;
        Cluster* _lastClus = _rowIdClusterMap[rowNum+j];
        int index = (_lastClus->_lastNode.find(rowNum+j))->second;
        int rightmost_x = _lastClus->_x_ref + _lastClus->_delta_x[index] + _lastClus->_modules[index]->_module->width();
        if(rightmost_x > maxX ) 
        {
            maxX = rightmost_x;
            if(rightmost_x > (_modPLPos[0][_cell->dbId()].x()))
            {
                _cluster = _lastClus;
            }
        }
    }
    if(_cluster == 0) // new cluster
    {
        return abs(_modPLPos[0][_cell->dbId()].y()-_cir->row_id_2_y(rowNum))+abs(_modPLPos[0][_cell->dbId()].x()-_cell->x());
    }
    else              // add to _cluster
    {
        //cout<<"Collapsed\n";
        Cluster *temp = new Cluster(*_cluster);
        //cout<<"_cluster size = "<<_cluster->_modules.size()<<endl;
        //cout<<"temp size = "<<temp->_modules.size()<<endl;
        AddCell_trial(temp, _cell, rowNum);
        //cout<<"temp size after = "<<temp->_modules.size()<<endl;
        temp = Collapse_trial(temp);//,i==18363);
        double cost = RenewCost(*temp) - temp->_cost;
        /*
        if(cost < 0 )
        {
            cout<<"Old Cost = "<<temp->_cost<<endl;
            cout<<"New Cost = "<<cost+temp->_cost<<"(should be larger)"<<endl;
        }
        */
        delete temp;
        return cost;
    }
}

void Placer::AddCell_trial(Cluster* _clus,Module* _cell, int _rowNum)
{
    //cout<<"AddCell\n";
    assert(_clus->_modules.size() != 0);
    assert(_cell->isStdCell()); //assert is standard cell (module includes preplaced blocks, I/O pins)
    int rowHeight = (int)(_cell->height()/_cir->rowHeight());
    Node* _newNode = new Node(_cell, rowHeight, _rowNum);
    _clus->_e += _cell->weight();   //numPins()

    //add module to _modules, _cellIdModuleMap
    _clus->_modules.push_back(_newNode);
    _clus->_cellIdModuleMap[_cell->dbId()] = _clus->_modules.size()-1;

    map<int, int>::iterator _iter;

    int delta_x = INT_MIN;
    // find previous node in cluster ( set "all"(not just adjacent ones) previous cells to FIs)->already modified 

    // renew prev_cells at the same time
    for(int i = 0; i < rowHeight ; i++)   
    {
        _iter = _clus->_lastNode.find(_rowNum+i);
        if(_iter != _clus->_lastNode.end())    //find previous node
        {
            Node* _prevNode = _clus->_modules[_iter->second];
            if(_clus->_delta_x[_iter->second]+_prevNode->_module->width() > delta_x)
            {
                delta_x = _clus->_delta_x[_iter->second]+_prevNode->_module->width();
            }
        }
    }

    assert(delta_x != INT_MIN);
    // renew _clus->_delta_x
    _clus->_delta_x.push_back(delta_x);

    // renew q
    _clus->_q += (_cell->weight())*(_modPLPos[0][_cell->dbId()].x()-delta_x);

    // renew _clus->_lastnode and _rowIdClusterMap
    /*for(int i = 0; i < rowHeight ; i++)   
    {
        // renew _clus->_lastnode
        _clus->_lastNode[_rowNum+i] = (_clus->_modules.size()-1);
    }*/

    // renew _x_ref
    set_x_to_site(_clus);
}

Cluster* Placer::Collapse_trial(Cluster* _clus)
{
    //cout<<"Collapse\n";
    pair<int,int> _overlap = CheckOverlap_trial(_clus);
    //cout<<"CheckOverlap done\n";
    if(get<0>(_overlap)!=0 || get<1>(_overlap)!=0){
        _clus = AddCluster_trial(&_cir->module(get<0>(_overlap)),&_cir->module(get<1>(_overlap)),_clus);
        _clus = Collapse_trial(_clus);
    }
    return _clus;
}

Cluster* Placer::AddCluster_trial(Module* _prevCell, Module* _cell, Cluster* _clus)
{
    //cout<<"AddCluster\n";
    Cluster* _prevClus = _cellIdClusterMap[_prevCell->dbId()];
    assert(_prevClus != 0 && _clus != 0 && _prevClus != _clus);
    int _prevNodeIndex = _prevClus->_cellIdModuleMap.find(_prevCell->dbId())->second;
    int _nodeIndex = _clus->_cellIdModuleMap.find(_cell->dbId())->second;

    //assert overlap is true
    assert(_prevClus->_x_ref+_prevClus->_delta_x[_prevNodeIndex]+_prevCell->width() > _clus->_x_ref+_clus->_delta_x[_nodeIndex]);

    double ref_dist = _prevClus->_delta_x[_prevNodeIndex]+_prevCell->width()-_clus->_delta_x[_nodeIndex];


    //renew _clus id
    //_clus->id = _prevClus->id;
    // why should the id be renewed?

    //renew e, q, delta_x, _prevClus->_modules, _cellIdModuleMap
    _clus->_e += _prevClus->_e;
    _clus->_q += _prevClus->_q + (_prevClus->_e)*(ref_dist);
    for(unsigned i = 0 ; i < _prevClus->_modules.size() ; i++)
    {
        _clus->_modules.push_back(_prevClus->_modules[i]);
        _clus->_delta_x.push_back(_prevClus->_delta_x[i]-ref_dist);
        _clus->_cellIdModuleMap[_prevClus->_modules[i]->_module->dbId()] = _clus->_modules.size()-1;
    }

    //re-establish _lastNode
    /*_clus->_lastNode.clear();
    vector<pair<int,int> > lastNodeTemp; //first: index in modules, second: delta_x+width
    lastNodeTemp.resize(_cir->numRows(),make_pair(-1,INT_MIN));
    for(unsigned i = 0 ; i < _clus->_modules.size() ; i++)
    {
        int rowId = _clus->_modules[i]->_rowId;
        for(int j = rowId ; j < rowId+_clus->_modules[i]->_degree ; j++)
        {
            if(lastNodeTemp[j].second < _clus->_delta_x[i]+_clus->_modules[i]->_module->width())
            {
                lastNodeTemp[j] = make_pair(i,_clus->_delta_x[i]+_clus->_modules[i]->_module->width()); 
            }
        }
    }
    for(unsigned i = 0 ; i < _cir->numRows() ; i++)
    {
        if(lastNodeTemp[i].second != INT_MIN)   //exist some node
        {
            _clus->_lastNode[i] = lastNodeTemp[i].first;
        }
    }*/

    //store _cost of _prevClus
    _clus->_cost += _prevClus->_cost;

    //re-evaluate position
    set_x_to_site(_clus);

    return _clus;
}

//try putting a "single" row height cell in the white space of _rowNum
//put the target cell in the first placeable dead space (i.e. first area that can accommodate the cell)
//return the cost (increased displacement - alpha*cell_area)(dead space removed == cell area)
//maybe try alpha = 0.1 for now
//return DBL_MAX when no such dead space
double reduce_DeadSpace_trial(Module* _cell, int _rowNum)
{
    return 0;
}

pair<int,int> Placer::CheckOverlap_trial(Cluster* _clus)
{
    //cout<<"CheckOverlap\n";
    pair<int,int> overlap;
    overlap = make_pair(0,0);
    int _x_max = 0;
    //map<int,int>::iterator iter ;

    for(unsigned i = 0 ; i < _clus->_modules.size() ; i++){
        //cout<<"size = "<<_clus->_modules.size()<<endl;
        //cout<<"i = "<<i<<endl;
        int ref = _clus->_modules[i]->_module->dbId(); //overlap cell id in _clus
        int ref_prev = -1;                             //overlap cell id in prev clus
        int row = _clus->_modules[i]->_rowId;
        for(int j = row ; j < row + _clus->_modules[i]->_degree ; j++){
            //iter = prev_cells[j].find(ref);
            //if(iter == prev_cells[j].end()) { /*cout<<"no prev"<<endl;*/continue; }
            //ref_prev = iter->second;    // if this cell is the first cell in the row ? ans: then find will return map::end()
            if(prev_cells[j][ref] == -1) { continue; }
            ref_prev = prev_cells[j][ref];

            //check if ref_prev is in the same cluster (_clus) or not
            if(_clus->_cellIdModuleMap.find(ref_prev) != _clus->_cellIdModuleMap.end()) { /*cout<<"same"<<endl;*/ continue;}
            //if(_cellIdClusterMap[ref_prev]->id != _clus->id){
            else{
                //cout<<"else"<<endl;
                int x_ref = _cellIdClusterMap[ref_prev]->_x_ref;
                x_ref += _cellIdClusterMap[ref_prev]->_delta_x[_cellIdClusterMap[ref_prev]->_cellIdModuleMap.find(ref_prev)->second];
                x_ref += _cir->module(ref_prev).width();

                //int x_ref_next = _clus->_delta_x[_clus->_cellIdModuleMap.find(ref_next)->second] + _clus->_x_ref;
                int x_ref_next = _clus->_delta_x[i] + _clus->_x_ref;

                if(x_ref_next < x_ref && (x_ref-x_ref_next) > _x_max){
                    _x_max = x_ref-x_ref_next;
                    overlap = make_pair(ref_prev,ref);
                }
            }
        }
    }
    if(_x_max != 0){
        //cout<<"Overlap = "<<_x_max<<endl;
        //cout<<"prev = "<<get<0>(overlap)<<endl;
        //cout<<"ref = "<<get<1>(overlap)<<endl;
        }
    return overlap; // return (0,0) if no overlap occurs
}