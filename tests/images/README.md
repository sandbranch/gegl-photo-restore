# Test photos

`fetch.py` downloads the photos of `manifest.json` from Wikimedia
Commons: ten CC0 photos for the synthetic fading (tests/photos.py) and
six faded slides of the EPA DOCUMERICA series, public domain, for the
look at real fading (tests/slides.py). The licence was read on each
file's Commons page; `manifest.json` names the page, author, licence and
the licence template found there (`{{Unsplash}}`, which includes
`{{Cc-zero}}`, for the photos; `{{PD-USGov-EPA}}` for the slides). They
come as Commons' 2560 pixel wide renditions (the original for
fruit-seller.jpg, 6000 x 4000, for the timing at 24 megapixels), and
each file is checked against the SHA-256 in the manifest. They stay out
of git to keep the repository small; `fetch.py` makes the same set
anywhere. Commons limits downloads by clients without contact details,
so the script waits when asked to and takes several minutes.

    tests/images/fetch.py

| File | What | Author | Licence |
|---|---|---|---|
| elderly-woman.jpg | Portrait of elderly woman | Christian Gertenbach | CC0 |
| market-produce.jpg | Farmer's Market Produce | Julian Hanslmaier | CC0 |
| newport-beach.jpg | People enjoying Newport Beach | Herson Rodriguez | CC0 |
| garden.jpg | In the garden | Darkness | CC0 |
| quaint-street.jpg | Quaint City Streets | Eduardo Arcos | CC0 |
| family-lake.jpg | Family standing by lake | Andre Hunter | CC0 |
| rural-family.jpg | Rural family in the field | Vladimir Kudinov | CC0 |
| fruit-seller.jpg | Street Fruit Seller at work | Igor Ovsyannykov | CC0 |
| tulips.jpg | Tulips standing at attention | Leigh Kendell | CC0 |
| taxi-rank.jpg | Taxi rank on city center street | Verne Ho | CC0 |
| slide-leakey-546097.jpg | Main street of Leakey (NARA 546097) | Marc St. Gil | Public domain |
| slide-chemical-546220.jpg | Chemical plants on shore are considered prime source of pollution (NARA 546220) | Marc St. Gil | Public domain |
| slide-crabbing-546222.jpg | Crabbing in polluted waters (NARA 546222) | Marc St. Gil | Public domain |
| slide-streetcar-546244.jpg | City street car (NARA 546244) | Marc St. Gil | Public domain |
| slide-brooklyn-546873.jpg | Children on a street in East Brooklyn (NARA 546873) | Jim Pickerell | Public domain |
| slide-havasupai-544312.jpg | Havasupai Indian children (NARA 544312) | Terry Eiler | Public domain |

No other photos are used by the tests, and the user's own photos never.
