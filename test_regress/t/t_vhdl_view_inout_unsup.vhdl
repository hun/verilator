-- DESCRIPTION: Verilator: VHDL interface view with an inout element
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package pack is
  type rec_t is record
    a : std_logic;
    b : std_logic;
  end record;

  view bus_view of rec_t is
    a : in;
    b : inout;
  end view;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.pack.all;

entity top is
  port (p : view bus_view);
end;

architecture rtl of top is
begin
end;
