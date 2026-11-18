# Test photos

`fetch.py` downloads the photos of `manifest.json` from Wikimedia
Commons: ten CC0 photos for the synthetic fading (tests/photos.py) and
15 faded slides of the EPA DOCUMERICA series (1972 to 1977), public
domain, for the look at real fading (tests/slides.py,
tests/slide-study.py). The nine added on 2026-11-18 were chosen by eye
among the 64 with the strongest mean cast of 450 DOCUMERICA files (the
first 30 of 15 photographers' categories): visibly faded (a red or
magenta cast over the whole frame), not merely red subjects, and
everyday scenes with people, buildings and grey objects, as family
slides have; one (the drinking water portrait) came in through EPA's
own Flickr account and has no photographer named. The licence was read on each
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
| slide-water-15194692141.jpg | A woman with a jar of drinking water ("1974 Safe Drinking Water Act", EPA's Flickr 15194692141) | unknown (US EPA) | Public domain |
| slide-beach-556300.jpg | A woman at Chicago's 12th Street Beach (NARA 556300) | John H. White | Public domain |
| slide-greasedpig-556536.jpg | A greased pig contest in Tennessee (NARA 556536) | Jack Corn | Public domain |
| slide-scouts-555899.jpg | Boy scout leader recruiting in Bedford-Stuyvesant (NARA 555899) | Danny Lyon | Public domain |
| slide-zionhouse-547036.jpg | Aftermath of severe storm in Zion (NARA 547036) | Paul Sequeira | Public domain |
| slide-zioncans-547039.jpg | Aftermath of severe storm in Zion, a bird on strewn garbage (NARA 547039) | Paul Sequeira | Public domain |
| slide-elpaso-545361.jpg | Alley off 3rd Avenue, El Paso (NARA 545361) | Danny Lyon | Public domain |
| slide-albuquerque-555987.jpg | Albuquerque train station (NARA 555987) | Charles O'Rear | Public domain |
| slide-seagull-545214.jpg | A young boy tries to save an injured seagull (NARA 545214) | Gene Daniels | Public domain |

No other photos are used by the tests, and the user's own photos never.
