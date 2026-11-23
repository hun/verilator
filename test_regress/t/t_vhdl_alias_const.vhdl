-- DESCRIPTION: Verilator: an alias of a constant used by a later constant
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    o : out natural;
    c : out character
  );
end entity t;

architecture rtl of t is
  constant str1 : string := "hello";
  alias str2 : string (str1'length downto 1) is str1;
  constant len  : natural := str2'length;
  constant ch   : character := str2(2);
begin
  o <= len;
  c <= ch;
end architecture rtl;
