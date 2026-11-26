-- DESCRIPTION: Verilator: a record range that calls a function reading a generic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  generic (BITS : natural := 6);
  port (d : in bit_vector(8 downto 0);
        q : out bit_vector(8 downto 0));
end entity t;

architecture rtl of t is
  constant STAGES : integer := 3;
  function wide return integer is
  begin
    return BITS + STAGES;
  end function wide;

  -- The widths come from a function of the architecture that reads a generic
  type pair_t is record
    i : bit_vector(wide - 1 downto 0);
    q : bit_vector(wide - 1 downto 0);
  end record;
  signal s : pair_t;
begin
  s.i <= d;
  s.q <= s.i;
  q <= s.q;
end architecture rtl;
