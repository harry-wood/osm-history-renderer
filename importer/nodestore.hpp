#ifndef IMPORTER_NODESTORE_HPP
#define IMPORTER_NODESTORE_HPP

class Nodestore {
public:
    struct Nodeinfo {
        double lat;
        double lon;
    };

private:
    typedef std::map< time_t, Nodeinfo > timemap;
    typedef std::pair< time_t, Nodeinfo > timepair;
    typedef std::map< time_t, Nodeinfo >::iterator timemap_it;
    typedef std::map< time_t, Nodeinfo >::const_iterator timemap_cit;

    typedef std::map< osm_object_id_t, timemap* > nodemap;
    typedef std::pair< osm_object_id_t, timemap* > nodepair;
    typedef std::map< osm_object_id_t, timemap* >::iterator nodemap_it;
    typedef std::map< osm_object_id_t, timemap* >::const_iterator nodemap_cit;
    nodemap m_nodemap;

    bool m_storeerrors;

public:
    Nodestore(bool storeerrors) : m_nodemap(), m_storeerrors(storeerrors) {}
    ~Nodestore() {
        nodemap_cit end = m_nodemap.end();
        for(nodemap_cit it = m_nodemap.begin(); it != end; ++it) {
            delete it->second;
        }
    }

    void record(osm_object_id_t id, osm_version_t v, time_t t, double lon, double lat) {
        Nodeinfo info = {lon, lat};

        nodemap_it it = m_nodemap.find(id);
        timemap *tmap;

        if(it == m_nodemap.end()) {
            if(Osmium::debug()) {
                std::cerr << "no timemap for node #" << id << ", creating new" << std::endl;
            }

            tmap = new timemap();
            m_nodemap.insert(nodepair(id, tmap));
        } else {
            tmap = it->second;
        }

        tmap->insert(timepair(t, info));
        if(Osmium::debug()) {
            std::cerr << "adding timepair for node #" << id << " v" << v << " at tstamp " << t << std::endl;
        }
    }

    Nodeinfo lookup(osm_object_id_t id, time_t t, bool &found) {
        if(Osmium::debug()) {
            std::cerr << "looking up information of node #" << id << " at tstamp " << t << std::endl;
        }

        nodemap_it nit = m_nodemap.find(id);
        if(nit == m_nodemap.end()) {
            if(m_storeerrors) {
                std::cerr << "no timemap for node #" << id << ", skipping node" << std::endl;
            }
            found = false;
            Nodeinfo nullinfo = {0, 0};
            return nullinfo;
        }

        timemap *tmap = nit->second;
        timemap_it tit = tmap->upper_bound(t);

        if(tit == tmap->begin()) {
            if(m_storeerrors) {
                std::cerr << "reference to node #" << id << " at tstamp " << t << " which is before the youngest available version of that node, using first version" << std::endl;
            }
        } else {
            tit--;
        }

        found = true;
        return tit->second;
    }

    geos::geom::Geometry* forgeGeometry(const Osmium::OSM::WayNodeList &nodes, time_t t, bool looksLikePolygon) {
        // shorthand to the geometry factory
        geos::geom::GeometryFactory *f = Osmium::Geometry::geos_geometry_factory();

        // pointer to coordinate vector
        std::vector<geos::geom::Coordinate> *c = new std::vector<geos::geom::Coordinate>();

        Osmium::OSM::WayNodeList::const_iterator end = nodes.end();
        for(Osmium::OSM::WayNodeList::const_iterator it = nodes.begin(); it != end; ++it) {
            osm_object_id_t id = it->ref();

            bool found;
            Nodeinfo info = lookup(id, t, found);
            if(!found)
                continue;

            if(Osmium::debug()) {
                std::cerr << "node #" << id << " at tstamp " << t << " references node at POINT(" << std::setprecision(8) << info.lat << ' ' << info.lon << ')' << std::endl;
            }
            
            c->push_back(geos::geom::Coordinate(info.lat, info.lon, DoubleNotANumber));
        }

        if(c->size() < 2) {
            if(m_storeerrors) {
                std::cerr << "found only " << c->size() << " valid coordinates, skipping way" << std::endl;
            }
            delete c;
            return NULL;
        }

        geos::geom::Geometry* geom;

        try {
            // tags say it could be a polygon and the way is closed
            if(looksLikePolygon && c->front() == c->back() && c->size() >= 4) {
                // build a polygon
                geom = f->createPolygon(
                    f->createLinearRing(
                        f->getCoordinateSequenceFactory()->create(c)
                    ),
                    NULL
                );
            } else {
                // build a linestring
                geom = f->createLineString(
                    f->getCoordinateSequenceFactory()->create(c)
                );
            }
        } catch(geos::util::GEOSException e) {
            if(m_storeerrors) {
                std::cerr << "error creating polygon: " << e.what() << std::endl;
            }
            delete c;
            return NULL;
        }

        geom->setSRID(900913);
        return geom;
    }

    std::vector<time_t> *calculateMinorTimes(const Osmium::OSM::WayNodeList &nodes, time_t from, time_t to) {
        std::vector<time_t> *minor_times = new std::vector<time_t>();

        for(Osmium::OSM::WayNodeList::const_iterator nodeit = nodes.begin(); nodeit != nodes.end(); nodeit++) {
            osm_object_id_t id = nodeit->ref();

            nodemap_it nit = m_nodemap.find(id);
            if(nit == m_nodemap.end()) {
                if(m_storeerrors) {
                    std::cerr << "no timemap for node #" << id << ", skipping node" << std::endl;
                }
                continue;
            }

            timemap *tmap = nit->second;
            timemap_cit lower = tmap->lower_bound(from);
            timemap_cit upper = to == 0 ? tmap->end() : tmap->upper_bound(to);
            for(timemap_cit it = lower; it != upper; it++) {
                minor_times->push_back(it->first);
            }
        }

        std::sort(minor_times->begin(), minor_times->end());
        std::unique(minor_times->begin(), minor_times->end());

        return minor_times;
    }

    std::vector<time_t> *calculateMinorTimes(const Osmium::OSM::WayNodeList &nodes, time_t from) {
        return calculateMinorTimes(nodes, from, 0);
    }
};

#endif // IMPORTER_NODESTORE_HPP
