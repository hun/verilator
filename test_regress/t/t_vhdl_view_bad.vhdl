-- DESCRIPTION: Verilator: VHDL interface view declaration diagnostics
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

  view no_elem of rec_t is
    a : in;
    x : in;
    b : out;
  end view;

  view dup_elem of rec_t is
    a : in;
    b : out;
    a : out;
  end view;

  view missing_elem of rec_t is
    a : in;
  end view;

  view not_record of std_logic is
    a : in;
  end view;

  type inner_t is record
    a : std_logic;
  end record;

  view inner_view of inner_t is
    a : in;
  end view;

  type outer_t is record
    r : inner_t;
    s : rec_t;
  end record;

  view nested_bad of outer_t is
    r : view no_such_view;
    s : view inner_view;
  end view;

  view ok_view of rec_t is
    a : in;
    b : out;
  end view;

end package;

library ieee;
use ieee.std_logic_1164.all;
use work.pack.all;

entity top is
  port (p : view no_such_view; q : in std_logic);
end;

architecture rtl of top is
  signal s : ok_view;
begin
end;
