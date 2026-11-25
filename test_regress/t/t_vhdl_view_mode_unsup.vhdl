-- DESCRIPTION: Verilator: VHDL interface view element with a linkage mode
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
  end record;

  view buf_view of rec_t is
    a : linkage;
  end view;
end package;

entity top is
end;

architecture rtl of top is
begin
end;
